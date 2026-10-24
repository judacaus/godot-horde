extends GutTest
## Headless tests for the Horde2D / HordeType node API.

const DT := 1.0 / 60.0


func _make_type(radius := 6.0, max_speed := 120.0) -> HordeType:
	var t := HordeType.new()
	t.radius = radius
	t.max_speed = max_speed
	t.acceleration = 2000.0
	t.max_hp = 10.0
	return t


func _make_horde() -> Horde2D:
	var horde := Horde2D.new()
	horde.update_mode = Horde2D.UPDATE_MANUAL
	horde.seed = 1
	add_child_autofree(horde)
	return horde


func test_classes_are_registered() -> void:
	assert_true(ClassDB.class_exists("Horde2D"))
	assert_true(ClassDB.class_exists("HordeType"))
	assert_true(ClassDB.is_parent_class("Horde2D", "Node2D"))
	assert_true(ClassDB.is_parent_class("HordeType", "Resource"))


func test_horde_type_defaults_and_frame_size() -> void:
	var t := HordeType.new()
	assert_eq(t.hframes, 1)
	assert_eq(t.modulate, Color.WHITE)
	t.radius = 5.0
	assert_eq(t.get_frame_size(), Vector2(10, 10), "no texture: body diameter")
	var img := Image.create(64, 16, false, Image.FORMAT_RGBA8)
	t.texture = ImageTexture.create_from_image(img)
	t.hframes = 4
	assert_eq(t.get_frame_size(), Vector2(16, 16))
	t.hframes = 0
	assert_eq(t.hframes, 1, "hframes is clamped to at least 1")


func test_spawn_returns_live_ids() -> void:
	var horde := _make_horde()
	var ids := horde.spawn(_make_type(), Vector2(100, 50), 25)
	assert_eq(ids.size(), 25)
	assert_eq(horde.get_agent_count(), 25)
	for id in ids:
		assert_true(horde.is_agent_alive(id))
		assert_lt(horde.get_agent_position(id).distance_to(Vector2(100, 50)), 60.0)
	var single := horde.spawn(_make_type(), Vector2(-5, 7))
	assert_eq(single.size(), 1)
	assert_eq(horde.get_agent_position(single[0]), Vector2(-5, 7))
	assert_eq(horde.spawn(_make_type(), Vector2.ZERO, 0).size(), 0)


func test_spawn_ring_places_agents_on_the_circle() -> void:
	var horde := _make_horde()
	var ids := horde.spawn_ring(_make_type(), Vector2(10, 20), 300.0, 64)
	assert_eq(ids.size(), 64)
	for id in ids:
		assert_almost_eq(horde.get_agent_position(id).distance_to(Vector2(10, 20)), 300.0, 0.01)


func test_agents_chase_a_target_node() -> void:
	var horde := _make_horde()
	var player := Node2D.new()
	add_child_autofree(player)
	player.position = Vector2(400, 0)
	horde.target = player
	assert_eq(horde.target, player)
	var ids := horde.spawn(_make_type(), Vector2.ZERO, 10)
	var start := _mean_distance(horde, ids, player.position)
	for i in 60:
		horde.step(DT)
	var end := _mean_distance(horde, ids, player.position)
	assert_lt(end, start - 80.0, "agents moved towards the target")
	# Moving the node retargets the horde on the next step.
	player.position = Vector2(-400, 0)
	var before := _mean_distance(horde, ids, player.position)
	for i in 60:
		horde.step(DT)
	assert_lt(_mean_distance(horde, ids, player.position), before - 80.0)


func test_target_position_is_used_without_target_node() -> void:
	var horde := _make_horde()
	horde.target_position = Vector2(0, -300)
	var ids := horde.spawn(_make_type(), Vector2.ZERO)
	for i in 30:
		horde.step(DT)
	assert_lt(horde.get_agent_position(ids[0]).y, -20.0)
	for i in 30:
		assert_lt(horde.get_agent_velocity(ids[0]).length(), 120.0 + 0.01, "max speed is respected")
		horde.step(DT)


func test_kill_makes_ids_stale_and_emits_one_batched_signal() -> void:
	var horde := _make_horde()
	watch_signals(horde)
	var ids := horde.spawn_ring(_make_type(), Vector2.ZERO, 200.0, 10)
	assert_true(horde.kill(ids[0]))
	assert_true(horde.kill(ids[1]))
	assert_true(horde.kill(ids[2]))
	assert_false(horde.kill(ids[2]), "killing twice fails")
	assert_false(horde.is_agent_alive(ids[0]))
	assert_eq(horde.get_agent_position(ids[0]), Vector2.ZERO, "stale ids return a zero position")
	assert_eq(horde.get_agent_count(), 7)
	assert_signal_not_emitted(horde, "agents_died", "deaths are batched until the next update")
	horde.step(DT)
	assert_signal_emit_count(horde, "agents_died", 1)
	var params: Array = get_signal_parameters(horde, "agents_died")
	var died: PackedInt32Array = params[0]
	var positions: PackedVector2Array = params[1]
	assert_eq(died.size(), 3)
	assert_eq(positions.size(), 3)
	assert_has(Array(died), ids[1])
	for p in positions:
		assert_almost_eq(p.length(), 200.0, 0.01)
	horde.step(DT)
	assert_signal_emit_count(horde, "agents_died", 1, "no new deaths, no new emission")
	# Slots get reused by new agents, but old ids must stay stale.
	horde.spawn(_make_type(), Vector2.ZERO, 50)
	assert_false(horde.is_agent_alive(ids[0]))


func test_damage_circle_hits_overlapping_agents_and_kills_at_zero_hp() -> void:
	var horde := _make_horde()
	watch_signals(horde)
	var t := _make_type(5.0)
	var a := horde.spawn(t, Vector2(0, 0))[0]
	var b := horde.spawn(t, Vector2(14, 0))[0] # 14 <= 10 + 5
	var c := horde.spawn(t, Vector2(16, 0))[0] # 16 > 15
	var hit := horde.damage_circle(Vector2.ZERO, 10.0, 4.0)
	assert_eq(hit.size(), 2)
	assert_has(Array(hit), a)
	assert_has(Array(hit), b)
	assert_almost_eq(horde.get_agent_hp(a), 6.0, 0.001)
	assert_almost_eq(horde.get_agent_hp(c), 10.0, 0.001)
	hit = horde.damage_circle(Vector2.ZERO, 10.0, 6.0)
	assert_eq(hit.size(), 2)
	assert_false(horde.is_agent_alive(a))
	assert_false(horde.is_agent_alive(b))
	assert_true(horde.is_agent_alive(c))
	assert_eq(horde.damage_circle(Vector2.ZERO, 10.0, 1.0).size(), 0, "dead agents are not hit again")
	horde.step(DT)
	assert_signal_emit_count(horde, "agents_died", 1)
	assert_eq((get_signal_parameters(horde, "agents_died")[0] as PackedInt32Array).size(), 2)


func test_query_circle_matches_a_gdscript_brute_force_scan() -> void:
	var horde := _make_horde()
	var rng := RandomNumberGenerator.new()
	rng.seed = 12345
	var small := _make_type(4.0)
	var big := _make_type(11.0)
	var ids := PackedInt32Array()
	for i in 800:
		var p := Vector2(rng.randf_range(-500, 500), rng.randf_range(-500, 500))
		ids.append_array(horde.spawn(small if i % 3 else big, p))
	for i in 20:
		horde.step(DT)
	for q in 60:
		var center := Vector2(rng.randf_range(-550, 550), rng.randf_range(-550, 550))
		var radius := rng.randf_range(0.0, 150.0)
		var expected := []
		for id in ids:
			var r: float = horde.get_agent_type(id).radius
			if horde.get_agent_position(id).distance_squared_to(center) <= (radius + r) * (radius + r):
				expected.append(id)
		var got := Array(horde.query_circle(center, radius))
		got.sort()
		expected.sort()
		# Positions cross into GDScript as doubles, so allow a hair of rounding at the edge.
		if got != expected:
			for id in got + expected:
				var d := horde.get_agent_position(id).distance_to(center) - radius - horde.get_agent_type(id).radius
				assert_almost_eq(d, 0.0, 0.001, "only boundary agents may differ")
		else:
			pass_test("query %d matches" % q)


func test_target_contact_is_emitted_once_per_update() -> void:
	var horde := _make_horde()
	horde.target_position = Vector2.ZERO
	horde.target_radius = 20.0
	watch_signals(horde)
	horde.spawn_ring(_make_type(6.0), Vector2.ZERO, 120.0, 8)
	for i in 120:
		horde.step(DT)
	assert_signal_emitted(horde, "target_contact")
	var count: int = get_signal_emit_count(horde, "target_contact")
	assert_lte(count, 120, "at most one emission per update")
	var params: Array = get_signal_parameters(horde, "target_contact")
	var ids: PackedInt32Array = params[0]
	assert_gt(ids.size(), 0)
	for id in ids:
		assert_lt(horde.get_agent_position(id).length(), 20.0 + 6.0 + 1.5)
	assert_almost_eq(params[1], DT, 0.000001, "delta is the time simulated by the update")


func test_target_contact_reports_simulated_time_and_skips_idle_updates() -> void:
	var horde := _make_horde()
	horde.target_position = Vector2.ZERO
	horde.target_radius = 20.0
	horde.fixed_timestep = 1.0 / 30.0
	horde.spawn(_make_type(6.0), Vector2(22, 0))
	watch_signals(horde)
	# A quarter of a fixed step: nothing is simulated, so no contact is reported even
	# though the agent already overlaps the target.
	horde.step(1.0 / 120.0)
	assert_signal_not_emitted(horde, "target_contact")
	# 1/120 + 1/10 s accumulated: three whole steps run in this update.
	horde.step(1.0 / 10.0)
	assert_signal_emit_count(horde, "target_contact", 1)
	var params: Array = get_signal_parameters(horde, "target_contact")
	assert_eq((params[0] as PackedInt32Array).size(), 1)
	assert_almost_eq(params[1], 3.0 / 30.0, 0.0001, "delta covers every step of the update")


func test_default_seed_is_the_same_as_an_explicit_zero_seed() -> void:
	var t := _make_type()
	var implicit := Horde2D.new()
	implicit.update_mode = Horde2D.UPDATE_MANUAL
	add_child_autofree(implicit)
	var explicit := Horde2D.new()
	explicit.update_mode = Horde2D.UPDATE_MANUAL
	explicit.seed = 0
	add_child_autofree(explicit)
	assert_eq(implicit.seed, 0)
	var a := implicit.spawn(t, Vector2.ZERO, 50)
	var b := explicit.spawn(t, Vector2.ZERO, 50)
	var positions_a := PackedVector2Array()
	var positions_b := PackedVector2Array()
	for k in a.size():
		positions_a.append(implicit.get_agent_position(a[k]))
		positions_b.append(explicit.get_agent_position(b[k]))
	assert_eq(positions_a, positions_b, "the default seed is really 0")


func test_bounds_and_obstacles_constrain_agents() -> void:
	var horde := _make_horde()
	horde.bounds = Rect2(-100, -100, 200, 200)
	horde.bounds_enabled = true
	horde.add_obstacle(Vector2(0, 0), 30.0)
	assert_eq(horde.get_obstacle_count(), 1)
	horde.target_position = Vector2(0, 0)
	horde.target_radius = 0.0
	var ids := horde.spawn_ring(_make_type(5.0, 300.0), Vector2.ZERO, 90.0, 40)
	for i in 90:
		horde.step(DT)
	for id in ids:
		var p := horde.get_agent_position(id)
		assert_between(p.x, -95.0, 95.0)
		assert_between(p.y, -95.0, 95.0)
		assert_gt(p.length(), 35.0 - 0.05, "outside the obstacle")
	horde.clear_obstacles()
	assert_eq(horde.get_obstacle_count(), 0)


func test_same_seed_and_inputs_are_deterministic() -> void:
	var a := _run_scripted_horde(7)
	var b := _run_scripted_horde(7)
	assert_eq(a, b)


func test_fixed_timestep_runs_whole_steps_only() -> void:
	var horde := _make_horde()
	horde.fixed_timestep = 1.0 / 30.0
	horde.spawn(_make_type(), Vector2.ZERO)
	horde.step(1.0 / 120.0)
	assert_eq(horde.get_debug_stats()["substeps"], 0)
	horde.step(1.0 / 30.0)
	assert_eq(horde.get_debug_stats()["substeps"], 1)


func test_clear_removes_agents_silently() -> void:
	var horde := _make_horde()
	watch_signals(horde)
	var ids := horde.spawn(_make_type(), Vector2.ZERO, 100)
	horde.clear()
	assert_eq(horde.get_agent_count(), 0)
	assert_false(horde.is_agent_alive(ids[0]))
	horde.step(DT)
	assert_signal_not_emitted(horde, "agents_died")


func test_debug_stats_report_the_last_update() -> void:
	var horde := _make_horde()
	horde.spawn(_make_type(), Vector2.ZERO, 100)
	horde.step(DT)
	var stats := horde.get_debug_stats()
	for key in ["agents", "types", "cell_size", "substeps", "neighbor_checks", "sim_usec", "render_usec", "update_usec"]:
		assert_has(stats, key)
	assert_eq(stats["agents"], 100)
	assert_eq(stats["types"], 1)


func test_process_mode_updates_automatically() -> void:
	var horde := Horde2D.new()
	horde.target_position = Vector2(500, 0)
	add_child_autofree(horde)
	var id := horde.spawn(_make_type(), Vector2.ZERO)[0]
	await wait_process_frames(10)
	assert_gt(horde.get_agent_position(id).x, 0.0)


func test_manual_mode_does_not_update_by_itself() -> void:
	var horde := _make_horde()
	horde.target_position = Vector2(500, 0)
	var id := horde.spawn(_make_type(), Vector2.ZERO)[0]
	await wait_process_frames(5)
	assert_eq(horde.get_agent_position(id), Vector2.ZERO)


func _mean_distance(horde: Horde2D, ids: PackedInt32Array, point: Vector2) -> float:
	var total := 0.0
	for id in ids:
		total += horde.get_agent_position(id).distance_to(point)
	return total / ids.size()


func _run_scripted_horde(seed_value: int) -> PackedVector2Array:
	var horde := Horde2D.new()
	horde.update_mode = Horde2D.UPDATE_MANUAL
	horde.seed = seed_value
	horde.target_radius = 12.0
	add_child_autofree(horde)
	var t := _make_type()
	var ids := horde.spawn(t, Vector2(200, 0), 300)
	ids.append_array(horde.spawn_ring(t, Vector2.ZERO, 400.0, 300))
	for i in 60:
		horde.target_position = Vector2(cos(i * 0.1), sin(i * 0.1)) * 50.0
		horde.damage_circle(Vector2(i * 4.0 - 100.0, 0), 25.0, 3.0)
		horde.step(DT)
	var out := PackedVector2Array()
	for id in ids:
		out.append(horde.get_agent_position(id))
	return out
