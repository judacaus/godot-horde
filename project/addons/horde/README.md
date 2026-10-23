# godot-horde

Thousands of enemies in Godot 4: a fast 2D horde/swarm system written in C++
(GDExtension) with a small node API for GDScript.

Status: pre-release (milestone 2 of v0.1). Requires Godot **4.4 or newer**.
Prebuilt binaries: Linux x86_64 for now (Windows, macOS and web are planned).

## Quick start

```gdscript
@onready var horde: Horde2D = $Horde2D

func _ready() -> void:
	var zombie := HordeType.new()
	zombie.texture = preload("res://zombie_walk.png") # Horizontal strip.
	zombie.hframes = 4
	zombie.fps = 8.0
	zombie.radius = 6.0
	zombie.max_speed = 90.0
	zombie.max_hp = 10.0

	horde.target = $Player # Any Node2D; or set horde.target_position.
	horde.spawn_ring(zombie, Vector2.ZERO, 800.0, 5000)
	horde.agents_died.connect(func(ids, positions): print(ids.size(), " died"))
	horde.target_contact.connect(func(ids): $Player.hurt(ids.size()))

func _on_explosion(at: Vector2) -> void:
	horde.damage_circle(at, 64.0, 25.0) # Returns the ids that were hit.
```

All positions are in the `Horde2D` node's local coordinates. Keep the node at the
origin with no transform if you want them to match global coordinates.

## Nodes and resources

**HordeType** (Resource): `texture`, `hframes`, `fps`, `modulate`, `radius`,
`max_hp`, `max_speed`, `acceleration`, `separation_weight`, `seek_weight`.

**Horde2D** (Node2D)

| Method | Description |
|---|---|
| `spawn(type, position, count = 1) -> PackedInt32Array` | Spawns agents (scattered in a small disc when `count > 1`). |
| `spawn_ring(type, center, radius, count) -> PackedInt32Array` | Spawns agents evenly on a circle. |
| `kill(id) -> bool` | Kills an agent. Its id becomes stale immediately. |
| `damage(id, amount) -> bool` | Damages one agent; kills it at hp <= 0. |
| `damage_circle(center, radius, amount) -> PackedInt32Array` | Damages every agent whose body overlaps the circle; returns the ids hit. |
| `query_circle(center, radius) -> PackedInt32Array` | Ids of agents whose body overlaps the circle. |
| `is_agent_alive(id)`, `get_agent_position(id)`, `get_agent_velocity(id)`, `get_agent_hp(id)`, `get_agent_type(id)` | Per-agent queries. Stale ids return zero values. |
| `get_agent_count()`, `clear()` | `clear()` removes all agents without emitting `agents_died`. |
| `add_obstacle(center, radius)`, `clear_obstacles()` | Static circular obstacles. |
| `step(delta)` | Advances the simulation; called automatically unless `update_mode` is Manual. |
| `get_debug_stats() -> Dictionary` | Timings and counters of the last update. |

Properties: `target`, `target_position`, `target_radius`, `target_solid`,
`update_mode` (Process / Physics / Manual), `fixed_timestep`, `max_substeps`,
`interpolate`, `seed`, `cell_size`, `overlap_resolution`, `bounds_enabled`, `bounds`,
`debug_draw`, `debug_draw_grid`.

Signals (at most one emission per update each):

- `agents_died(ids: PackedInt32Array, positions: PackedVector2Array)`
- `target_contact(ids: PackedInt32Array)`: agents touching the target this update.

**HordeDebugOverlay** (`addons/horde/debug/horde_debug_overlay.gd`): add it to a scene
and assign `horde` to see FPS and update timings. F3 toggles the in-world debug drawing.

## Determinism

With the same `seed`, the same calls in the same order and a fixed timestep (or
manual stepping with constant deltas), the simulation is bit-identical between runs
of the same build. Bit equality across platforms or compilers is not guaranteed.

## License

MIT © 2026 judacaus. See `LICENSE`.
