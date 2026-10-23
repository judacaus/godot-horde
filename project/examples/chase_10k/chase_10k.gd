extends Node2D
## Minimal Horde2D example: 10,000 agents chase a moving target.
## The target orbits on its own; hold the arrow keys to steer it. Agents touching the
## target lose hit points, and every agent that dies respawns far away, so the
## population stays constant.

const AGENT_COUNT := 10000

@onready var horde: Horde2D = $Horde2D
@onready var player: Node2D = $Player

var _time := 0.0
var _enemy: HordeType


func _ready() -> void:
	_enemy = HordeType.new()
	_enemy.texture = _make_blob_texture()
	_enemy.hframes = 4
	_enemy.fps = 8.0
	_enemy.radius = 6.0
	_enemy.max_speed = 90.0
	_enemy.max_hp = 30.0
	_enemy.modulate = Color(1.0, 0.6, 0.5)
	# 50 clusters of 200 agents scattered on an annulus around the target.
	var rng := RandomNumberGenerator.new()
	rng.seed = 42
	for i in 50:
		var angle := rng.randf() * TAU
		var distance := rng.randf_range(700.0, 1600.0)
		horde.spawn(_enemy, Vector2.from_angle(angle) * distance, AGENT_COUNT / 50)
	horde.target_contact.connect(_on_target_contact)
	horde.agents_died.connect(_on_agents_died)


func _process(delta: float) -> void:
	_time += delta
	var input := Input.get_vector("ui_left", "ui_right", "ui_up", "ui_down")
	if input != Vector2.ZERO:
		player.position += input * 220.0 * delta
	else:
		player.position = Vector2(cos(_time * 0.35), sin(_time * 0.35)) * 350.0
	queue_redraw()


func _draw() -> void:
	draw_circle(player.position, horde.target_radius, Color(0.4, 0.8, 1.0))


func _on_target_contact(ids: PackedInt32Array) -> void:
	for id in ids:
		horde.damage(id, 1.0)


func _on_agents_died(ids: PackedInt32Array, _positions: PackedVector2Array) -> void:
	horde.spawn_ring(_enemy, player.position, 1500.0, ids.size())


# Four 16x16 frames of a bobbing blob, generated so the example ships no assets.
static func _make_blob_texture() -> Texture2D:
	var img := Image.create(64, 16, false, Image.FORMAT_RGBA8)
	for frame in 4:
		var squash := [0.0, 0.6, 1.0, 0.6][frame] as float
		var rx := 6.0 + squash
		var ry := 7.0 - squash
		var center := Vector2(8.0, 9.0 + squash)
		for y in 16:
			for x in 16:
				var d := Vector2((x + 0.5 - center.x) / rx, (y + 0.5 - center.y) / ry)
				if d.length_squared() <= 1.0:
					var shade := 1.0 - 0.35 * clampf(d.y, 0.0, 1.0)
					img.set_pixel(frame * 16 + x, y, Color(shade, shade, shade))
		for eye_x in [6, 10]:
			img.set_pixel(frame * 16 + eye_x, int(center.y) - 2, Color.BLACK)
	return ImageTexture.create_from_image(img)
