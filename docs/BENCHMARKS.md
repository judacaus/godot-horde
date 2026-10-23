# Benchmarks

All numbers below were measured on 2026-10-23 in the development environment described
here. Nothing is estimated. Raw CSVs live in [`project/bench/results/`](../project/bench/results/).

## Environment

| | |
|---|---|
| CPU | Intel Xeon Processor @ 2.10 GHz, 4 vCPUs (1 thread per core), AVX2/AVX-512, L2 8 MiB (4 instances) |
| Machine | Cloud VM (Firecracker kernel `6.18.44-fc-v153`, Docker container), 15 GiB RAM, **no GPU** |
| OS / compiler | Ubuntu 24.04.5 LTS, GCC 13.3.0, SCons 4.11.1 |
| Godot | 4.7.2-stable official (`ed1daf0bf`), headless (`--headless`: dummy renderer) |
| godot-horde | commit of this PR, godot-cpp 10.0.0-stable, `api_version=4.4` |

The machine is shared, so expect noise: see "Run-to-run variation" below.

## Method

`project/bench/bench.tscn` runs two implementations of the same behaviour on the same
seeded starting positions:

- **Horde2D**: one `Horde2D` in manual update mode; the bench calls `step(1/60)` once
  per frame. One `HordeType` (radius 6 px, max speed 90 px/s, acceleration 600 px/s²,
  16×16 texture), solid target radius 12 px, default separation and overlap settings.
- **GDScript baseline** (the usual "one node per enemy" approach): per agent a
  `CharacterBody2D` (floating motion mode) with a `CollisionShape2D` (shared
  `CircleShape2D`, radius 6) and a `Sprite2D`, and a script whose `_physics_process`
  seeks the target with the same max speed and acceleration and calls
  `move_and_slide()`. Bodies collide with each other (layer 1 / mask 1).

The target moves on a circle of radius 150 px around the origin at 0.5 rad/s.

Scenarios:

- **spread**: agents uniform in an annulus (inner radius 200 px, ~20×20 px per agent).
  During the measurement the crowd is still converging, so contacts are few.
- **dense**: agents on a hexagonal grid (spacing 2.1 × radius) filling the disc around
  the target. Every agent pushes on its neighbours from the first frame. Worst case
  for both implementations.

For each (scenario, implementation, N) the bench runs 60 warm-up frames, then measures
300 frames or 30 s (minimum 30 frames), with `--fixed-fps 60` so every frame advances
exactly 1/60 s and runs exactly one physics tick.

Measured per frame:

- **update ms** — Horde2D: wall time of `Horde2D.step()`, i.e. simulation + writing
  the MultiMesh buffer + signal emission (split into **sim / render** from
  `get_debug_stats()`). Baseline: wall time from the start of the physics tick to the
  start of the idle frame, i.e. all `_physics_process` callbacks plus the 2D physics
  server step. (`Performance.TIME_PHYSICS_PROCESS` is not usable for this: it reports
  the maximum over the last second, refreshed once per second.)
- **frame ms** — wall time between consecutive frames. In headless mode it is within
  0.1 ms of update ms for both implementations, so the tables only show update ms; the
  CSVs have both.

Two configurations:

1. **Editor binary + `template_debug` library** (`-O2`, debug checks): what you get
   when running from the editor or `godot --headless`.
2. **Release export**: the project exported with the official `linux_release.x86_64`
   template and the `template_release` library (`-O3`). The bench scene is selected
   through the `horde_bench` custom feature (`run/main_scene.horde_bench`).

### Reproduce

```sh
scons -j4 && scons -j4 target=template_release
tools/fetch_godot.sh 4.7.2
# 1. Editor binary + debug library
tools/run_bench.sh .tools/godot-4.7.2/Godot_v4.7.2-stable_linux.x86_64 out_debug.csv \
    --sizes=1000,5000,10000,20000 --frames=300 --warmup=60 --budget=30
# 2. Release export (needs the 4.7.2 export templates installed)
.tools/godot-4.7.2/Godot_v4.7.2-stable_linux.x86_64 --headless --path project \
    --export-release "Linux Benchmark" "$PWD/.tools/export/linux/horde_bench.x86_64"
.tools/export/linux/horde_bench.x86_64 --headless --fixed-fps 60 -- \
    --sizes=1000,5000,10000,20000 --frames=300 --warmup=60 --budget=30 --out="$PWD/out_release.csv"
```

## Results

Update time in milliseconds per frame (lower is better). "Speed-up" is baseline mean
divided by Horde2D mean. "Baseline frames" is how many frames fit in the 30 s budget.

### 1. Editor binary + template_debug library

`2026-10-23_linux-x86_64_editor-4.7.2_template-debug.csv`

**spread**

| N | Horde2D mean / p95 | Horde2D sim / render | Baseline mean / p95 | Speed-up | Baseline frames |
|---:|---:|---:|---:|---:|---:|
| 1,000 | 0.23 / 0.31 | 0.21 / 0.02 | 9.3 / 21.3 | 39× | 300 |
| 5,000 | 1.00 / 1.19 | 0.86 / 0.13 | 36.2 / 49.4 | 36× | 300 |
| 10,000 | 2.08 / 2.41 | 1.77 / 0.30 | 72.6 / 101.8 | 35× | 300 |
| 20,000 | 4.25 / 5.64 | 3.61 / 0.63 | 169.3 / 190.4 | 40× | 178 |

**dense**

| N | Horde2D mean / p95 | Horde2D sim / render | Baseline mean / p95 | Speed-up | Baseline frames |
|---:|---:|---:|---:|---:|---:|
| 1,000 | 0.32 / 0.35 | 0.30 / 0.02 | 27.3 / 36.2 | 86× | 300 |
| 5,000 | 1.75 / 2.02 | 1.60 / 0.14 | 167.9 / 200.0 | 96× | 179 |
| 10,000 | 3.60 / 4.38 | 3.31 / 0.28 | 350.5 / 387.4 | 97× | 86 |
| 20,000 | 7.18 / 8.74 | 6.58 / 0.59 | 754.9 / 828.8 | 105× | 40 |

### 2. Release export + template_release library

`2026-10-23_linux-x86_64_export-release-4.7.2_template-release.csv`

**spread**

| N | Horde2D mean / p95 | Horde2D sim / render | Baseline mean / p95 | Speed-up | Baseline frames |
|---:|---:|---:|---:|---:|---:|
| 1,000 | 0.22 / 0.30 | 0.21 / 0.01 | 10.2 / 20.7 | 46× | 300 |
| 5,000 | 1.09 / 1.50 | 0.95 / 0.13 | 39.1 / 52.3 | 36× | 300 |
| 10,000 | 1.97 / 2.21 | 1.70 / 0.27 | 71.8 / 105.1 | 36× | 300 |
| 20,000 | 4.19 / 5.73 | 3.60 / 0.58 | 169.8 / 193.2 | 41× | 177 |

**dense**

| N | Horde2D mean / p95 | Horde2D sim / render | Baseline mean / p95 | Speed-up | Baseline frames |
|---:|---:|---:|---:|---:|---:|
| 1,000 | 0.32 / 0.38 | 0.30 / 0.02 | 28.2 / 38.6 | 89× | 300 |
| 5,000 | 1.66 / 1.88 | 1.52 / 0.13 | 170.0 / 216.8 | 103× | 177 |
| 10,000 | 3.71 / 4.53 | 3.42 / 0.28 | 360.6 / 417.9 | 97× | 84 |
| 20,000 | 6.90 / 8.15 | 6.33 / 0.56 | 958.5 / 1058.5 | 139× | 32 |

The release export is not measurably faster than configuration 1 on this machine,
for either implementation; differences are within the run-to-run noise below. The
baseline's 958 ms at dense 20k (vs 755 ms in configuration 1) comes from only 32
frames and should be read as noise until re-measured on dedicated hardware.

### Run-to-run variation

Configuration 1 was run twice. The first run overlapped with a `git clone` for its
first case; the second run, reported above, had the machine to itself.

| Case | Run 1 | Run 2 |
|---|---:|---:|
| spread, Horde2D, 10k / 20k | 1.97 / 4.00 ms | 2.08 / 4.25 ms |
| dense, Horde2D, 10k / 20k | 3.69 / 7.01 ms | 3.60 / 7.18 ms |
| spread, baseline, 10k / 20k | 65.0 / 159.2 ms | 72.6 / 169.3 ms |
| dense, baseline, 10k / 20k | 347.8 / 729.9 ms | 350.5 / 754.9 ms |

Expect roughly ±10 % between runs on this VM.

## Reading the numbers

- At 60 FPS the frame budget is 16.7 ms. On this CPU Horde2D stays under 8.8 ms (p95)
  with **20,000** agents in the worst (dense) case, and under 2.5 ms (p95) with 10,000
  agents in the spread case.
- The GDScript + `CharacterBody2D` baseline exceeds the budget with **1,000** bodies
  in the dense case (27–28 ms) and somewhere between 1,000 and 5,000 bodies in the
  spread case (9–10 ms at 1k, 36–39 ms at 5k).
- Horde2D scales roughly linearly with N (≈0.35 µs per agent per frame dense,
  ≈0.2 µs spread). The baseline grows faster than linearly in the dense case.
- Writing the MultiMesh buffers (render) is 8–15 % of Horde2D's update; the rest is
  the simulation.

## What these numbers do not include

- **GPU and real rendering cost.** Headless mode uses a dummy rendering server, so
  `multimesh_set_buffer` uploads and draw calls cost (almost) nothing here, and the
  baseline's 20,000 `Sprite2D` draws cost nothing either. Rendering benchmarks on real
  GPUs are a milestone 3 item.
- Multiple physics ticks per frame, game logic, or signal handlers doing work.
- Other CPUs or operating systems.

## Other measured observations (not benchmarks)

- **Core only** (no Godot; `g++ -O2`, same dense layout, 240 measured steps after 60
  warm-up steps, two runs): 6.06 and 6.29 ms per step at 20,000 agents, 3.35 ms at
  10,000. A callgrind profile of the 20,000 case attributes ≈73 % of instructions to
  `Simulation::compute_steering` (9 hashed bucket lookups per agent), ≈7 % to the hash
  rebuild and ≈5 % to integration and constraints. This points at the first
  optimisation for milestone 3: hash cells so that the three cells of a row land in
  consecutive buckets (3 range scans instead of 9 lookups) and keep the dense arrays
  sorted by cell.
- **Example scene with software rendering**: `examples/chase_10k` (10,000 agents)
  under Xvfb with Mesa llvmpipe (OpenGL 4.5, Compatibility renderer, CPU-only) showed
  49 FPS and a 4.92 ms Horde2D update (4.0 sim + 0.9 render) in the debug overlay.
- **Web**: the web smoke test (5,000 agents, release build, headless Chromium with
  SwiftShader WebGL) reported Horde2D updates between 3.7 and 5.8 ms at frame 120
  across four runs. One sample per run, used only to confirm the build works; see
  docs/PLAN.md.
