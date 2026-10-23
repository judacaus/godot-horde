# CLAUDE.md

Guidance for AI agents (and humans) working on **godot-horde**: a 2D horde/swarm
system for Godot 4, written as a GDExtension in C++ (godot-cpp) with a node API for
GDScript. MIT licensed, © 2026 judacaus.

Scope, milestones and research notes live in [docs/PLAN.md](docs/PLAN.md); measured
performance lives in [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

## Working rules

- Code, comments, commit messages and documentation are written in **English**.
  Talk to the maintainer (judacaus) in **Spanish**.
- Work on a branch, keep commits small and focused, open a PR. Never force-push `main`.
- No paid dependencies and no external services beyond GitHub (Actions, releases).
- Do not publish anything outside the repository (forums, Asset Library, GitHub
  releases): the maintainer does that.
- Filter long build output (errors and relevant warnings only).
- If something blocks you (network, toolchain), stop, explain the blocker and propose
  alternatives instead of improvising.
- Report measured numbers only. Never estimate performance figures.

## Layout

```
SConstruct, methods.py     Build (from godotengine/godot-cpp-template).
godot-cpp/                 Submodule, tag 10.0.0-stable.
src/core/                  Engine-agnostic C++17 simulation. No Godot includes.
src/godot/                 GDExtension bindings (Horde2D, HordeType, registration).
tests/core/                doctest suite for src/core (thirdparty/doctest, vendored).
project/                   The single Godot project used for development:
  addons/horde/            The distributable add-on: .gdextension, bin/, debug/, LICENSE, README.
  tests/                   GUT tests (headless). GUT itself is fetched, not vendored.
  examples/chase_10k/      Minimal example: 10,000 agents chase a moving target.
  bench/                   Headless benchmark vs a CharacterBody2D baseline (CSV output).
  demo/survivors/          Playable demo (milestone 4, not started).
tools/                     fetch_godot.sh, install_gut.sh, run_godot_tests.sh, run_bench.sh,
                           web_smoke/ (web export + headless Chromium check).
docs/                      PLAN.md, BENCHMARKS.md.
.github/workflows/ci.yml   Linux CI: core tests, build, headless tests on 4.7.2 and 4.4.1.
```

## Commands

```sh
git submodule update --init                 # godot-cpp
pip install scons                           # SCons >= 4.0

scons -j4                                   # Extension, template_debug, host platform.
                                            # Installs into project/addons/horde/bin/<platform>/
scons -j4 target=template_release           # Optimized library for exported games.
scons tests -j4                             # Build + run core unit tests (doctest).
scons tests sanitize=yes -j4                # Same with ASan + UBSan.

tools/fetch_godot.sh 4.7.2                  # Official Godot build into .tools/ (SHA-512 checked).
tools/install_gut.sh 4.7.2                  # GUT version matching the Godot minor version.
tools/run_godot_tests.sh .tools/godot-4.7.2/Godot_v4.7.2-stable_linux.x86_64
tools/run_bench.sh .tools/godot-4.7.2/Godot_v4.7.2-stable_linux.x86_64 out.csv \
    --sizes=1000,5000,10000,20000 --layouts=spread,dense

# Web (single-threaded export). Use the Emscripten version of the official templates:
# 4.0.20 for Godot 4.6/4.7, 4.0.10 for 4.5, 3.1.64 for 4.3/4.4.
source <emsdk>/emsdk_env.sh
scons -j4 platform=web target=template_release threads=no
tools/web_smoke/run.sh .tools/godot-4.7.2/Godot_v4.7.2-stable_linux.x86_64   # needs export templates
```

Switching between 32-bit (web) and 64-bit targets in one checkout regenerates the
godot-cpp bindings automatically (marker in `godot-cpp/gen/.horde_bindings_bits`), which
rebuilds godot-cpp (~3–4 min on 4 cores).

Useful one-offs:

- Check that the extension loads:
  `godot --headless --path project --script res://tests/check_extension_loads.gd`
- Visual check without a GPU (Mesa llvmpipe):
  `xvfb-run -a godot --path project --rendering-driver opengl3`
- Filter SCons output: `scons -j4 2>&1 | grep -E "error|warning:" | grep -v "^g++"`.

## Key decisions

- **Minimum Godot version: 4.4** (`compatibility_minimum = "4.4"`, godot-cpp
  `api_version = "4.4"`). It is the lowest version allowed by the project brief, and
  godot-cpp 10 supports 4.3+. Building against the 4.4 API is a compile-time proof that
  every engine method we call exists in 4.4, and CI runs the GUT suite on the official
  4.4.1 and 4.7.2 builds. Nothing in v0.1 needs 4.5+ APIs; raise the minimum only
  for a concrete need.
- **godot-cpp 10.0.0-stable**, the first release versioned independently of Godot,
  which is what makes `api_version` selectable.
- **SCons only.** The template also ships CMake; we dropped it to maintain one build.
- **Core/binding split.** `src/core` is plain C++17 (no exceptions, no RTTI needs, no
  Godot types) so it can be unit-tested fast and reused. Bindings convert at the edge.
- **Agent ids** are 31-bit ints: 20-bit slot + 11-bit generation (generation 0 never
  used, so id 0 and -1 are never valid). Slots are reused FIFO and retired instead of
  wrapping, so a stale id can never alias a newer agent. Max 1,048,576 live slots.
- **Kills only flag agents.** Dense arrays are compacted at the next step, so ids,
  queries and the spatial hash stay valid between steps without rebuilds.
- **Spatial hash:** uniform cells (default 2 × max radius) hashed into a power-of-two
  table built with a counting sort. Entries store their exact cell, so hash collisions
  never produce duplicates. Agents spawned after the last build form a small "tail"
  that queries scan linearly. `query_circle` uses circle-vs-body overlap
  (`distance <= radius + agent_radius`).
- **Steering:** desired velocity = (seek × seek_weight + separation × separation_weight)
  × max_speed, clamped to max_speed, approached with bounded acceleration. A positional
  overlap correction (`overlap_resolution`, default 0.8) moves overlapping agents apart;
  the one farther from the target yields more. Without that priority the crowd's
  pressure accumulates towards the target and the core collapses.
- **Determinism:** same inputs + call order + build ⇒ bit-identical state (tested).
  Seeded PCG32, no unordered iteration in the hot path, `-ffp-contract=off` on core
  sources. Cross-platform bit equality is not promised (libm `sin/cos` in spawn_ring).
- **Rendering:** one MultiMesh (unit QuadMesh, 2D transform + color + custom data) per
  HordeType, each on a child canvas item of the Horde2D, written with exactly one
  `multimesh_set_buffer` per type per update plus a custom AABB (skips the engine's
  AABB recomputation). The QuadMesh is built for 3D (+Y up) so instance transforms
  negate Y. A canvas_item shader animates the strip from `INSTANCE_CUSTOM`
  (x: frame offset, y: fps, z: hframes). The Compatibility renderer packs instance
  color/custom data as half floats, so keep those values small.
- **Signals are batched:** `agents_died(ids, positions)` and `target_contact(ids)` are
  emitted at most once per update, after rendering.
- **Single Godot project.** Tests, examples, the benchmark and (later) the demo live in
  `project/` next to `addons/horde`, instead of top-level `demo/` and `bench/` folders:
  a Godot scene must live inside a project, and one project avoids duplicating the
  add-on binaries. Benchmark CSVs go to `project/bench/results/` (has `.gdignore`).
- **Web:** works with the "Extensions Support" export option and a `threads=no` build;
  no COOP/COEP headers needed for single-threaded exports, but a secure context is.
  Evidence and limitations: docs/PLAN.md, "Web export spike".
- **GUT is fetched, not vendored**, because each GUT release targets one Godot minor
  version (9.4.0 → 4.3/4.4, 9.7.1 → 4.7).

## Conventions

- Follow the Godot code style: tabs, `.clang-format` from the template, `snake_case`,
  `p_` parameters, `r_` output parameters. Core classes use a trailing `_` for members.
- Core code must not include Godot headers, throw exceptions or use global state.
- Every core change comes with doctest coverage; behaviour visible from GDScript gets a
  GUT test. Keep the hash-vs-brute-force and determinism tests passing.
- Public API changes must update the class docs/README and `docs/PLAN.md` if scope moves.
- Commit messages: imperative subject line, wrapped body explaining why.
- `.uid` files generated by Godot 4.4+ are committed with their resources.
