# godot-horde

Thousands of enemies in Godot 4: a fast 2D horde/swarm system written in C++
(GDExtension) with a small node API for GDScript.

Status: pre-release (milestone 2 of v0.1). Requires Godot **4.4 or newer**
(tested on 4.4.1 and 4.7.2). The full reference is built into the editor: press
**F1** and search for `Horde2D` or `HordeType`.

## Installation

There are no published binaries yet, so for now you build the add-on yourself:

1. Build it from the repository root (see the root README):
   `scons -j4 && scons -j4 target=template_release`. This writes the libraries into
   `project/addons/horde/bin/<platform>/`.
2. Copy the whole `addons/horde` folder, including `bin/`, into your project's
   `addons/` folder.
3. Restart the editor. `Horde2D` and `HordeType` appear in the Add Node and New
   Resource dialogs. No plugin needs to be enabled.

Only Linux x86_64 is built and tested in CI so far. Windows, macOS and Android
entries already exist in `horde.gdextension`, but those builds are not validated
yet; the web build is covered in [Web export](#web-export).

## Quick start

Add a `Horde2D` node to your scene (or create it from code), then:

```gdscript
@onready var horde: Horde2D = $Horde2D

func _ready() -> void:
	var zombie := HordeType.new()
	zombie.texture = preload("res://zombie_walk.png") # 4 frames side by side.
	zombie.hframes = 4
	zombie.fps = 8.0
	zombie.radius = 6.0
	zombie.max_speed = 90.0
	zombie.max_hp = 10.0

	horde.target = $Player # Any Node2D; or set horde.target_position.
	horde.spawn_ring(zombie, Vector2.ZERO, 800.0, 5000)
	horde.agents_died.connect(_on_agents_died)
	horde.target_contact.connect(func(ids): $Player.hurt(ids.size()))

func _on_explosion(at: Vector2) -> void:
	horde.damage_circle(at, 64.0, 25.0) # Returns the ids that were hit.

func _on_agents_died(ids: PackedInt32Array, positions: PackedVector2Array) -> void:
	for p in positions:
		drop_xp_gem(p)
```

`project/examples/chase_10k` in the repository is a complete scene with 10,000
agents.

## How it works

- **Agents are not nodes.** Each agent is an integer id returned by `spawn` and
  `spawn_ring`. When an agent dies its id becomes *stale* forever: it is never
  reused for another agent, and methods taking it return `false` or a zero value.
  Use `is_agent_alive(id)` when in doubt.
- **Local coordinates.** Every position (spawning, queries, signals, target) is in
  the `Horde2D` node's local space. Keep the node at the origin with no rotation or
  scale if you want global coordinates.
- **Updates.** By default the horde advances once per frame (`update_mode` =
  Process). Physics mode advances once per physics tick, and Manual mode only when
  you call `step(delta)`. Each update simulates, draws, then emits `agents_died` and
  `target_contact`, at most once each.
- **Sprites.** A `HordeType` texture is a horizontal strip of `hframes` frames drawn
  at its native size; `radius` is the collision body, it does not scale the sprite.
  Without a texture, agents are squares of the body diameter tinted with `modulate`.
- **Movement.** Agents seek the target, steer away from overlapping neighbours and
  are pushed apart when they overlap (the one farther from the target yields), so
  crowds pack around the target instead of collapsing onto it.

## Reference

### HordeType (Resource)

| Property | Default | Description |
|---|---|---|
| `texture` | `null` | Horizontal sprite strip. `null` draws a square. |
| `hframes` | `1` | Frames in the strip. |
| `fps` | `8.0` | Animation speed. Agents start at random frames; `0` keeps each agent on its spawn frame. |
| `modulate` | white | Tint for all agents of the type. |
| `radius` | `8.0` | Body radius in px (separation, obstacles, bounds, queries, contacts). |
| `max_hp` | `10.0` | Hit points at spawn. |
| `max_speed` | `80.0` | px/s. |
| `acceleration` | `600.0` | px/s². High values turn and stop almost instantly. |
| `separation_weight` | `1.5` | Steering away from neighbours. `0`: this type ignores others. |
| `seek_weight` | `1.0` | Steering towards the target. `0`: no seeking; negative: flee. |

Movement and body changes apply to living agents on the next update; `max_hp` is
read at spawn.

### Horde2D (Node2D): methods

| Method | Description |
|---|---|
| `spawn(type, position, count = 1) -> PackedInt32Array` | Spawns agents, scattered in a small disc when `count > 1`. |
| `spawn_ring(type, center, radius, count) -> PackedInt32Array` | Spawns agents evenly on a circle. |
| `kill(id) -> bool` | Kills an agent. Its id becomes stale immediately. |
| `damage(id, amount) -> bool` | Damages one agent; kills it at hp <= 0. Negative amounts heal up to `max_hp`. |
| `damage_circle(center, radius, amount) -> PackedInt32Array` | Damages every agent whose body overlaps the circle; returns the ids hit (dead ones included). |
| `query_circle(center, radius) -> PackedInt32Array` | Ids of agents whose body overlaps the circle (distance <= radius + agent radius). |
| `is_agent_alive(id)`, `get_agent_position(id)`, `get_agent_velocity(id)`, `get_agent_hp(id)`, `get_agent_type(id)` | Per-agent queries. Stale ids return `false`, zero vectors, `0.0` or `null`. |
| `get_agent_count()` | Living agents. |
| `clear()` | Removes all agents without emitting `agents_died` for them. |
| `add_obstacle(center, radius) -> int`, `clear_obstacles()`, `get_obstacle_count()` | Static circular obstacles. |
| `step(delta)` | Advances, draws and emits signals. Automatic unless `update_mode` is Manual. |
| `get_debug_stats() -> Dictionary` | Counters and timings of the last update. |

### Horde2D: properties

| Property | Default | Description |
|---|---|---|
| `target` | `null` | Node2D to chase. Falls back to `target_position` when unset or outside the tree. |
| `target_position` | `(0, 0)` | Point to chase when there is no `target`. |
| `target_radius` | `16.0` | Target size. Agents within `target_radius + radius + 1` px are reported by `target_contact`. `0` disables contacts. |
| `target_solid` | `true` | Agents cannot enter the target's circle and press around it. |
| `update_mode` | Process | Process, Physics or Manual (call `step`). |
| `fixed_timestep` | `0.0` | If > 0, simulate in whole steps of this length (frame-rate independent, reproducible). |
| `max_substeps` | `8` | Cap on fixed steps per update; extra time is dropped. |
| `interpolate` | `true` | With a fixed timestep, draw between the last two simulated positions. |
| `seed` | `0` | Random seed for spawn scatter, ring angle and animation offsets. |
| `cell_size` | `0.0` | Spatial hash cell in px. `0` = twice the largest radius. |
| `overlap_resolution` | `0.8` | Fraction (0–1) of each overlap removed per step. |
| `bounds_enabled`, `bounds` | `false`, `Rect2(-512, -512, 1024, 1024)` | Keep agents inside a rectangle. |
| `debug_draw`, `debug_draw_grid` | `false`, `true` | Draw bounds, obstacles, target radius and the hash grid. |

### Signals

- `agents_died(ids: PackedInt32Array, positions: PackedVector2Array)`: every agent
  that died since the last update, with its last position. The ids are already stale.
- `target_contact(ids: PackedInt32Array)`: agents touching the target this update.
  Agents in contact are reported every update, so add your own damage cooldown.

### Debug overlay

`addons/horde/debug/horde_debug_overlay.gd` (`HordeDebugOverlay`, a CanvasLayer):
add it to a scene and assign its `horde` property to see FPS, agent count and
update timings. F3 toggles the in-world debug drawing.

### Custom shaders

Set a `material` on the `Horde2D` node to replace the built-in sprite shader. Per
instance, the shader receives `INSTANCE_CUSTOM.x` (frame offset), `.y` (frames per
second) and `.z` (frame count), and the type's `modulate` as the instance color
(already multiplied into `COLOR`). The built-in shader is:

```glsl
shader_type canvas_item;

void vertex() {
	float hframes = max(INSTANCE_CUSTOM.z, 1.0);
	float frame = mod(floor(TIME * INSTANCE_CUSTOM.y + INSTANCE_CUSTOM.x), hframes);
	UV.x = (UV.x + frame) / hframes;
}
```

## Determinism

With the same `seed`, the same calls in the same order and a fixed timestep (or
manual stepping with constant deltas), the simulation is bit-identical between runs
of the same build. Bit equality across platforms or compilers is not guaranteed.

## Web export

Tested end to end with Godot 4.7.2 and 4.4.1 (details in `docs/PLAN.md` of the
repository):

1. Build the web library with the Emscripten version used by the official templates
   (4.0.20 for Godot 4.6/4.7, 4.0.10 for 4.5, 3.1.64 for 4.4):
   `scons platform=web target=template_release threads=no`.
2. In the Web export preset, enable **Extensions Support**. Leave **Thread Support**
   off (the default); a threaded export needs a separate `threads=yes` build.
3. Serve the game over HTTPS (or `localhost` while testing). Single-threaded exports
   do not need the COOP/COEP headers.

The Extensions Support templates add about 6 MB (uncompressed) to the download.

## Limitations

- No pathfinding: agents walk straight at the target and slide around obstacles,
  so they can get stuck behind large ones.
- One target per `Horde2D` (use several nodes for several targets).
- Sprites do not rotate; they flip horizontally to face where they move.
- Single-threaded simulation.
- Up to 1,048,576 agents per `Horde2D`.
- Only horizontal sprite strips (`hframes`, no `vframes`).
- 2D only.

## Troubleshooting

- **`Horde2D` is not in the Add Node dialog / "No GDExtension library found"**: the
  library for your platform is missing from `addons/horde/bin/`. Build it (see
  Installation).
- **Agents don't move**: check `update_mode` (Manual needs `step`), `max_speed`,
  `seek_weight`, and that `target` is inside the scene tree.
- **Web: "GDExtension libraries are not supported by this engine version"**: enable
  Extensions Support in the export preset.
- **Web: "Secure Context - Check web server configuration (use HTTPS)"**: serve over
  HTTPS or from `localhost`.

## License

MIT © 2026 judacaus. See `LICENSE`.
