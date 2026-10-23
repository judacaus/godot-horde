extends Node
## Headless benchmark: Horde2D versus a GDScript + CharacterBody2D baseline.
##
## Run (see tools/run_bench.sh):
##   godot --headless --fixed-fps 60 --path project res://bench/bench.tscn -- \
##       --sizes=1000,5000,10000,20000 --impls=horde,gdscript --layouts=spread,dense \
##       --out=/abs/path.csv
##
## Layouts (identical seeded positions for both implementations):
##   spread  Uniform in an annulus (about 20x20 px per agent): the crowd is still
##           converging on the target during the measurement, few contacts.
##   dense   Hexagonal packing in a disc around the target (spacing 2.1 radii):
##           every agent presses on its neighbours from the first frame.
##
## For every (implementation, N) pair it spawns N agents with the same seeded layout,
## warms up, then records per frame:
##   update_ms  Horde2D: wall time of Horde2D.step() (simulation + MultiMesh buffer
##              write + signal emission). Baseline: wall time from the start of the
##              physics tick (this node has the lowest physics priority) to the start
##              of the idle frame (this node has the lowest process priority), i.e.
##              every _physics_process() callback plus the 2D physics server step.
##              (Performance.TIME_PHYSICS_PROCESS is not usable here: it reports the
##              maximum over the last second, refreshed once per second.)
##   frame_ms   Wall time between consecutive frames (whole engine iteration).
## --fixed-fps 60 makes every frame advance exactly 1/60 s with one physics tick.

const DT := 1.0 / 60.0
const AGENT_RADIUS := 6.0
const AGENT_SPEED := 90.0
const AGENT_ACCELERATION := 600.0

var sizes: Array[int] = [1000, 5000, 10000, 20000]
var impls: Array[String] = ["horde", "gdscript"]
var layouts: Array[String] = ["spread", "dense"]
var warmup_frames := 60
var measure_frames := 300
var budget_sec := 30.0
var out_path := ""

var _runs: Array[Dictionary] = []
var _results: Array[Dictionary] = []
var _run_index := -1
var _world: Node2D
var _target: Node2D
var _horde: Horde2D
var _frame := 0
var _measure_start_usec := 0
var _last_frame_usec := 0
var _update_samples := PackedFloat64Array()
var _frame_samples := PackedFloat64Array()
var _time := 0.0
var _agent_texture: Texture2D
var _agent_shape: CircleShape2D
var _physics_start_usec := 0


func _ready() -> void:
	process_priority = -1000000
	process_physics_priority = -1000000
	_parse_args()
	for layout in layouts:
		for impl in impls:
			for n in sizes:
				_runs.append({"impl": impl, "n": n, "layout": layout})
	var img := Image.create(16, 16, false, Image.FORMAT_RGBA8)
	img.fill(Color.WHITE)
	_agent_texture = ImageTexture.create_from_image(img)
	_agent_shape = CircleShape2D.new()
	_agent_shape.radius = AGENT_RADIUS
	print("godot-horde benchmark | Godot %s | %s | %d CPU threads | fixed-fps %d" % [
		Engine.get_version_info().string, OS.get_processor_name(), OS.get_processor_count(), Engine.physics_ticks_per_second])
	_next_run()


func _parse_args() -> void:
	for arg in OS.get_cmdline_user_args():
		var parts := arg.trim_prefix("--").split("=", true, 1)
		if parts.size() != 2:
			continue
		match parts[0]:
			"sizes":
				sizes.clear()
				for s in parts[1].split(","):
					sizes.append(int(s))
			"impls":
				impls.clear()
				for s in parts[1].split(","):
					impls.append(s)
			"layouts":
				layouts.clear()
				for s in parts[1].split(","):
					layouts.append(s)
			"warmup":
				warmup_frames = int(parts[1])
			"frames":
				measure_frames = int(parts[1])
			"budget":
				budget_sec = float(parts[1])
			"out":
				out_path = parts[1]


func _next_run() -> void:
	if _world:
		_world.queue_free()
		_world = null
		_horde = null
	_run_index += 1
	if _run_index >= _runs.size():
		_finish()
		return
	var run := _runs[_run_index]
	_world = Node2D.new()
	add_child(_world)
	_target = Node2D.new()
	_world.add_child(_target)
	_spawn(run["impl"], run["n"], run["layout"])
	_frame = 0
	_time = 0.0
	_update_samples.clear()
	_frame_samples.clear()
	_last_frame_usec = 0


func _positions(n: int, layout: String) -> PackedVector2Array:
	var out := PackedVector2Array()
	if layout == "dense":
		# Hexagonal grid clipped to the smallest disc holding n points.
		var spacing := AGENT_RADIUS * 2.1
		var radius := spacing * sqrt(n / (PI / (sqrt(3.0) / 2.0))) * 1.05 + spacing
		var row_height := spacing * sqrt(3.0) / 2.0
		var candidates := []
		var rows := int(radius / row_height) + 1
		var cols := int(radius / spacing) + 1
		for j in range(-rows, rows + 1):
			for i in range(-cols, cols + 1):
				var p := Vector2(i * spacing + (spacing * 0.5 if j % 2 else 0.0), j * row_height)
				candidates.append([p.length_squared(), p])
		candidates.sort_custom(func(a, b): return a[0] < b[0] or (a[0] == b[0] and (a[1].x < b[1].x or (a[1].x == b[1].x and a[1].y < b[1].y))))
		for k in n:
			out.append(candidates[k][1])
		return out
	var rng := RandomNumberGenerator.new()
	rng.seed = 20261023
	var outer := sqrt(float(n)) * 20.0 + 200.0
	out.resize(n)
	for i in n:
		var r := sqrt(rng.randf_range(200.0 * 200.0, outer * outer))
		out[i] = Vector2.from_angle(rng.randf() * TAU) * r
	return out


func _spawn(impl: String, n: int, layout: String) -> void:
	var positions := _positions(n, layout)
	if impl == "horde":
		_horde = Horde2D.new()
		_horde.update_mode = Horde2D.UPDATE_MANUAL
		_horde.target = _target
		_horde.target_radius = 12.0
		_world.add_child(_horde)
		var t := HordeType.new()
		t.texture = _agent_texture
		t.radius = AGENT_RADIUS
		t.max_speed = AGENT_SPEED
		t.acceleration = AGENT_ACCELERATION
		for p in positions:
			_horde.spawn(t, p)
	else:
		var script: Script = load("res://bench/baseline_agent.gd")
		for p in positions:
			var body := CharacterBody2D.new()
			body.motion_mode = CharacterBody2D.MOTION_MODE_FLOATING
			body.position = p
			body.set_script(script)
			body.target = _target
			body.max_speed = AGENT_SPEED
			body.acceleration = AGENT_ACCELERATION
			var shape := CollisionShape2D.new()
			shape.shape = _agent_shape
			body.add_child(shape)
			var sprite := Sprite2D.new()
			sprite.texture = _agent_texture
			body.add_child(sprite)
			_world.add_child(body)


func _physics_process(_delta: float) -> void:
	_physics_start_usec = Time.get_ticks_usec()


func _process(_delta: float) -> void:
	if _world == null:
		return
	var now := Time.get_ticks_usec()
	var run := _runs[_run_index]
	# The target circles the origin so the crowd keeps moving.
	_time += DT
	_target.position = Vector2(cos(_time * 0.5), sin(_time * 0.5)) * 150.0

	var update_usec := 0.0
	if run["impl"] == "horde":
		var t0 := Time.get_ticks_usec()
		_horde.step(DT)
		update_usec = Time.get_ticks_usec() - t0
	else:
		update_usec = now - _physics_start_usec

	_frame += 1
	if _frame == warmup_frames:
		_measure_start_usec = now
	elif _frame > warmup_frames:
		_update_samples.append(update_usec / 1000.0)
		_frame_samples.append((now - _last_frame_usec) / 1000.0)
		var elapsed := (now - _measure_start_usec) / 1e6
		var count := _update_samples.size()
		if count >= measure_frames or (elapsed >= budget_sec and count >= 30):
			_record(run)
			_next_run()
			return
	_last_frame_usec = now


func _record(run: Dictionary) -> void:
	var update := _stats(_update_samples)
	var frame := _stats(_frame_samples)
	var result := {
		"layout": run["layout"],
		"impl": run["impl"],
		"n": run["n"],
		"frames": _update_samples.size(),
		"update_ms_mean": update[0], "update_ms_p50": update[1], "update_ms_p95": update[2],
		"frame_ms_mean": frame[0], "frame_ms_p50": frame[1], "frame_ms_p95": frame[2],
	}
	_results.append(result)
	print("%-6s %-8s N=%6d  update %8.3f ms (p50 %8.3f, p95 %8.3f)  frame %8.3f ms (p95 %8.3f)  [%d frames]" % [
		run["layout"], run["impl"], run["n"], update[0], update[1], update[2], frame[0], frame[2], _update_samples.size()])


# Returns [mean, p50, p95].
func _stats(samples: PackedFloat64Array) -> Array[float]:
	var sorted := samples.duplicate()
	sorted.sort()
	var total := 0.0
	for s in sorted:
		total += s
	var n := sorted.size()
	return [total / n, sorted[int(n * 0.5)], sorted[mini(int(n * 0.95), n - 1)]]


func _finish() -> void:
	var lines := PackedStringArray()
	lines.append("layout,impl,n,frames,update_ms_mean,update_ms_p50,update_ms_p95,frame_ms_mean,frame_ms_p50,frame_ms_p95,godot_version,cpu,threads")
	for r in _results:
		lines.append("%s,%s,%d,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%s,\"%s\",%d" % [
			r["layout"], r["impl"], r["n"], r["frames"],
			r["update_ms_mean"], r["update_ms_p50"], r["update_ms_p95"],
			r["frame_ms_mean"], r["frame_ms_p50"], r["frame_ms_p95"],
			Engine.get_version_info().string, OS.get_processor_name(), OS.get_processor_count()])
	var csv := "\n".join(lines) + "\n"
	if out_path != "":
		var f := FileAccess.open(out_path, FileAccess.WRITE)
		if f:
			f.store_string(csv)
			f.close()
			print("CSV written to ", out_path)
		else:
			push_error("Cannot write " + out_path)
	print(csv)
	get_tree().quit()
