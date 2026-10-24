class_name HordeDebugOverlay
extends CanvasLayer
## Screen-space statistics for a [Horde2D]: FPS, agent count and update timings.
## Press F3 to toggle the in-world debug drawing (spatial hash grid, bounds,
## obstacles and target radius).

## The horde to inspect.
@export var horde: Horde2D
## Draws the spatial hash grid and shapes in the world (same as [member Horde2D.debug_draw]).
@export var show_world_debug := false:
	set(value):
		show_world_debug = value
		if is_instance_valid(horde):
			horde.debug_draw = value

var _label: Label


func _ready() -> void:
	layer = 100
	_label = Label.new()
	_label.position = Vector2(8, 8)
	_label.add_theme_color_override("font_outline_color", Color.BLACK)
	_label.add_theme_constant_override("outline_size", 4)
	add_child(_label)
	show_world_debug = show_world_debug


func _unhandled_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if key and key.pressed and not key.echo and key.keycode == KEY_F3:
		show_world_debug = not show_world_debug


func _process(_delta: float) -> void:
	if not is_instance_valid(horde):
		_label.text = "HordeDebugOverlay: no Horde2D assigned"
		return
	var s := horde.get_debug_stats()
	_label.text = "FPS %d\nAgents %d\nUpdate %.2f ms (sim %.2f, render %.2f)\nSubsteps %d | neighbour checks %d | cell %.1f px\nF3: world debug" % [
		Engine.get_frames_per_second(),
		s["agents"],
		s["update_usec"] / 1000.0,
		s["sim_usec"] / 1000.0,
		s["render_usec"] / 1000.0,
		s["substeps"],
		s["neighbor_checks"],
		s["cell_size"],
	]
