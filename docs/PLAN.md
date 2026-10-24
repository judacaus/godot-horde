# godot-horde plan

## Why this project exists

Context checked on 2026-10-23:

- [godotengine/godot-proposals#2380](https://github.com/godotengine/godot-proposals/issues/2380)
  asks for a way to handle large numbers of entities (507 reactions, open since 2021,
  locked in February 2026). A Godot team member suggested doing it as an add-on.
- The team opened the megathread
  [godot-proposals discussion #14198](https://github.com/godotengine/godot-proposals/discussions/14198)
  and the "experiments project"
  ([discussion #14199](https://github.com/godotengine/godot-proposals/discussions/14199)),
  which invites GDExtension solutions that beat GDScript demos. It has no replies yet.
- Existing work: GECS (generic ECS in GDScript), godex (C++ ECS, inactive since 2023),
  BlastBullets2D (bullets only). There is no focused, maintained horde system.

godot-horde aims to be that focused system: thousands of simple 2D agents (enemies,
critters, crowds) simulated in C++ and drawn with one MultiMesh per agent type,
driven from GDScript through a small node API.

## v0.1 scope (2D only)

Must have:

- `Horde2D` (Node2D) rendering through a MultiMesh (QuadMesh + texture/sprite strip
  animated by a shader through `INSTANCE_CUSTOM`), writing the buffer with a single
  call per frame (per agent type).
- `HordeType` resource: texture, hframes, fps, radius, max_speed, acceleration,
  max_hp, separation_weight, seek_weight, modulate.
- C++ simulation: SoA layout, stable handles with generation counters, uniform spatial
  hash, seek (towards a Node2D or a position) + separation + max speed, optional
  deterministic fixed step.
- API: `spawn(type, pos, count=1) -> PackedInt32Array`,
  `spawn_ring(type, center, radius, count)`, `kill(id)`,
  `damage_circle(center, radius, amount) -> PackedInt32Array`,
  `query_circle(center, radius) -> PackedInt32Array`, `get_agent_position(id)`,
  `get_agent_count()`, `clear()`.
- Batched signals, one emission per frame: `agents_died(ids: PackedInt32Array,
  positions: PackedVector2Array)` and `target_contact(ids: PackedInt32Array,
  delta: float)` (`delta`: simulated time, so contact damage is frame-rate independent).
- Rectangular bounds and static circular obstacles.
- Debug overlay (stats and optional grid).

Out of scope for v0.1: 3D, flow fields/navigation, physics engine integration,
multithreading, compute shaders, networking.

## Milestones

| # | Milestone | Status |
|---|-----------|--------|
| 1 | Foundations: scaffold from godot-cpp-template, Linux build, headless load check, CLAUDE.md + this plan, web export spike, Linux CI | Done (this PR) |
| 2 | Core + minimal Horde2D: C++ core with doctest (hash == brute force, stale handles), Horde2D with 10k agents chasing a target, headless benchmark vs GDScript, docs/BENCHMARKS.md | Done (this PR) |
| 3 | Hardening and platforms (proposal below) | Next |
| 4 | Survivors demo, documentation, packaging, v0.1.0 release candidate | Later |

### What milestones 1–2 delivered

Beyond the brief, most of the v0.1 API already exists because it is thin on top of the
core: every method and signal listed above, bounds, obstacles, solid target with
contacts, fixed timestep with render interpolation, deterministic seed, update modes
(process / physics / manual), in-world debug drawing and `HordeDebugOverlay`
(screen-space stats). User documentation: class reference in `doc_classes/` (editor
help) and the add-on README (installation, reference tables, web export,
limitations, troubleshooting). What is missing for v0.1 is tracked in milestone 3.

### Milestone 3 proposal: hardening and platforms

1. **Windows and macOS** in CI (build + headless GUT run), and a first run of the
   adapted `make_build.yml` (manual all-platform build, added in this PR but not
   exercised yet because `workflow_dispatch` needs it on `main`).
2. **Web build in CI**: `platform=web threads=no` with the template's Emscripten
   version, plus `tools/web_smoke/run.sh` (export + headless Chromium, no COOP/COEP
   headers) against the 4.4 and 4.7 templates. A `threads=yes` build for threaded
   exports, tested with the headers.
3. **Rendering benchmark on real GPUs** (Forward+ and Compatibility), with a release
   export of the benchmark; headless numbers here exclude GPU cost.
4. **API polish found while building milestone 2**: editor icons, interpolation in
   `UPDATE_PHYSICS` mode using `Engine.get_physics_interpolation_fraction()`, per-type
   draw order, hit-flash via the free `INSTANCE_CUSTOM.w` channel, a contact
   cooldown option, `damage_circle` returning only killed ids as an option.
5. **Robustness**: fuzz the binding with random API sequences in GUT, a test for
   double-precision builds, and a stress test at 100k agents.
6. **Performance pass** guided by the benchmark: reorder dense arrays by cell to
   improve locality, skip the overlap pass for agents with no neighbours, measure
   `multimesh_set_buffer` cost on GPU drivers.

### Milestone 4 (outline)

`project/demo/survivors` playable demo, README with GIFs, add-on README and docs,
`.gitattributes` `export-ignore` so the Asset Library zip only contains
`addons/horde`, release checklist. Publishing is done by the maintainer.

## Decisions

See CLAUDE.md ("Key decisions") for the full list. Highlights:

- **Minimum Godot 4.4.** Lowest version allowed by the brief; godot-cpp 10 supports
  4.3+. Built against the 4.4 extension API (compile-time proof that every method we
  call exists in 4.4) and tested on the official 4.4.1 and 4.7.2 builds in CI.
- **godot-cpp 10.0.0-stable** with `api_version=4.4`.
- **One Godot project** (`project/`) holding the add-on, tests, examples, benchmark and
  demo, instead of separate top-level `demo/` and `bench/` projects.
- **Crowd model:** soft separation steering plus a prioritized positional overlap
  correction (the agent farther from the target yields). Plain symmetric separation
  let 200 agents of radius 8 collapse into a disc of radius 57 (dense packing needs
  about 120); with the correction the measured radius is 127–142 with no pair
  overlapping by more than half a body (core test "A crowd spreads out").

## Web export spike: GDExtension on the web

Question: can a game that uses godot-horde be exported to the web, and under which
requirements and limitations? Everything below is taken from the official
documentation, the engine source of the exact version we test (4.7.2-stable,
commit `ed1daf0bf`), godot-cpp 10.0.0 or issue reports, or was measured here.

### What the sources say

1. **Extensions need the dlink templates and the "Extensions Support" option.**
   The docs: "If your project uses GDExtension, **Extension Support** needs to be
   enabled" and "GDExtensions still need to be specifically compiled for the web
   platform to work" ([godot-docs `tutorials/export/exporting_for_web.rst`](https://github.com/godotengine/godot-docs/blob/master/tutorials/export/exporting_for_web.rst),
   section *Thread and extension support*). In the engine source the template name is
   `web[_dlink][_nothreads]_{debug,release}.zip`, with `_dlink` chosen by
   `variant/extensions_support` (`platform/web/export/export_plugin.h`,
   `_get_template_name`). The dlink templates are built with `dlink_enabled=yes`
   (`-sSIDE_MODULE=2`, `WEB_DLINK_ENABLED`; `platform/web/detect.py`) and the extension
   `.wasm` files are passed to Emscripten as `dynamicLibraries`
   (`platform/web/js/engine/config.js`).
2. **Threads and no-threads exports need different extension binaries.** The export
   adds the feature tags `threads`/`nothreads` and `web_extensions`
   (`export_plugin.cpp`, `get_preset_features`). godot-cpp builds a `SIDE_MODULE=1`
   with `-sUSE_PTHREADS=1` unless `threads=no`, which also appends `.nothreads` to the
   file name (`godot-cpp/tools/web.py`, `tools/godotcpp.py`). Mixing them fails at
   load time with `LinkError: ... "env" "memory": mismatch in shared state of memory`;
   the workaround reported is separate `threads=yes/no` builds selected by the
   `.gdextension` keys ([godotengine/godot#94537](https://github.com/godotengine/godot/issues/94537), open).
3. **Emscripten version must match the templates.** Godot 4.7's official web
   templates are built with Emscripten **4.0.20**
   ([godotengine/build-containers, branch 4.7, `Dockerfile.web`](https://github.com/godotengine/build-containers/blob/4.7/Dockerfile.web)).
   #94537 also shows signature mismatches (e.g. `std::chrono`, `strtoll`) when the
   side module and the engine disagree about the C++ runtime.
4. **Cross-origin isolation: the docs and the code disagree.** The docs say that
   enabling Extensions Support, "like thread support, … requires the use of
   cross-origin isolation headers". In the 4.7.2 source, the page only checks
   cross-origin isolation and `SharedArrayBuffer` when `threads` is enabled
   (`platform/web/js/engine/features.js`, `getMissingFeatures`), and the HTML shell
   passes the export's thread setting. A secure context (HTTPS or localhost) is still
   required even without threads: [godotengine/godot#124636](https://github.com/godotengine/godot/issues/124636)
   (open, 4.7.2) reports no-threads exports refusing to start over plain `http://`
   from a LAN address.
5. **Single-threaded export is the recommended default since 4.3** (docs, note at the
   top of the page), which is also what godot-horde needs: v0.1 is single-threaded.

### Empirical validation (2026-10-23)

Setup: godot-horde built with `scons platform=web target=template_release threads=no`
using Emscripten 4.0.20 (`libhorde.web.template_release.wasm32.nothreads.wasm`,
1,106,500 bytes). A minimal project (the add-on plus a scene that creates a `Horde2D`,
spawns 5,000 agents, steps 120 frames, calls `damage_circle` and reports through
`JavaScriptBridge`) was exported headless with the official templates and loaded in
headless Chromium (Playwright 1.64 driving the preinstalled Chromium, WebGL2 through
SwiftShader). The page was served by `python3 -m http.server`, which sends **no**
COOP/COEP headers.

| Export | Served from | Result |
|---|---|---|
| 4.7.2, Extensions Support **on**, Thread Support off | `http://127.0.0.1` | **Works.** Console: `Build configuration: Emscripten 4.0.20, single-threaded, GDExtension support.` Extension loaded, classes registered, 5,000 agents simulated, `damage_circle` hit 3,147 agents and `agents_died` delivered 3,147 ids. Page reports `crossOriginIsolated == false` and no `SharedArrayBuffer`. Rendering checked with a screenshot. |
| 4.7.2, Extensions Support **off** (control) | `http://127.0.0.1` | Engine refuses to start: `GDExtension libraries are not supported by this engine version. Enable "Extensions Support" for your export preset and/or build your custom template with "dlink_enabled=yes".` |
| 4.7.2, Extensions Support on | `http://192.0.2.2` (LAN address, not a secure context) | Refuses to start: `Secure Context - Check web server configuration (use HTTPS)`, as reported in #124636. |
| **4.4.1** templates (Emscripten 3.1.64), Extensions Support on, same `.wasm` | `http://127.0.0.1` | **Works**, same checks as the first row. |

Export size (uncompressed): with Extensions Support the template ships
`index.side.wasm` (44,078,870 bytes) + `index.wasm` (1,508,095 bytes) instead of a
single `index.wasm` (39,514,754 bytes): about +6 MB before compression, plus our
1.1 MB side module.

### Conclusions for godot-horde

- **Web export works with GDExtension**, verified end to end on 4.7.2 and 4.4.1.
  Requirements: enable **Extensions Support** in the Web export preset (dlink
  templates), ship a `threads=no` build for the default single-threaded export, and
  serve from a secure context (HTTPS, or localhost while testing).
- **COOP/COEP headers are not needed for single-threaded exports**, despite the docs
  sentence above; the 4.7.2 source and our test agree. Threaded exports still need
  them, and a separate `threads=yes` build (not tested here; #94537 documents the
  failure mode when variants are mixed). We should report the docs discrepancy
  upstream (the maintainer decides; we do not post outside the repo).
- `horde.gdextension` now selects web libraries with the `nothreads`/`threads`
  feature tags, so a threaded export can never load the single-threaded build.
- **Emscripten version**: build with the version of the official templates.
  Templates use 3.1.64 for Godot 4.3/4.4, 4.0.10 for 4.5 and 4.0.20 for 4.6/4.7
  ([build-containers](https://github.com/godotengine/build-containers) branches
  4.3–4.7, `Dockerfile.web`). Our 4.0.20 build also loaded in 4.4.1, but that is a
  single data point (the extension uses little of the C++ standard library:
  containers, `<algorithm>`, `<cmath>`);
  #94537 shows other libraries breaking on C++ runtime mismatches. CI should test
  every supported template version.
- **Limitations to document for users**: no threads (fine for v0.1, which is
  single-threaded anyway), larger download (+6 MB uncompressed for the dlink template),
  secure context required, and one binary per thread variant.
- **Local pitfall found**: godot-cpp bakes the pointer size into `godot-cpp/gen` and
  does not regenerate when switching between web (32-bit) and 64-bit builds in the
  same checkout. The SConstruct now records the bitness of the last build and forces
  `generate_bindings` when it changes.
