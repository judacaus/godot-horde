# godot-horde

Thousands of enemies in Godot 4: a fast 2D swarm/horde system (GDExtension) with a
node-based API. Work in progress; see [docs/PLAN.md](docs/PLAN.md).

- C++ simulation (structure of arrays, generational ids, uniform spatial hash, seek +
  separation, deterministic fixed step) drawn with one MultiMesh per agent type.
- GDScript API: `Horde2D` node and `HordeType` resource, batched signals.
- Godot 4.4+ (tested on 4.4.1 and 4.7.2).

Usage: [project/addons/horde/README.md](project/addons/horde/README.md). The class
reference is also built into the editor help (F1, search `Horde2D`).
Performance: [docs/BENCHMARKS.md](docs/BENCHMARKS.md).
Contributing and build commands: [CLAUDE.md](CLAUDE.md).

## Building

```sh
git clone --recursive https://github.com/judacaus/godot-horde
cd godot-horde
pip install scons
scons -j4            # builds and installs into project/addons/horde/bin/
scons tests          # core unit tests
```

Open `project/` in Godot 4.4+ and run the main scene (10,000 agents chasing a target).

Based on [godotengine/godot-cpp-template](https://github.com/godotengine/godot-cpp-template).

## License

MIT © 2026 judacaus.
