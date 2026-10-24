extends GutTest
## Smoke tests for the example scenes shipped in project/examples.


func test_chase_10k_scene_runs() -> void:
	var scene: PackedScene = load("res://examples/chase_10k/chase_10k.tscn")
	var root: Node2D = scene.instantiate()
	add_child_autofree(root)
	var horde: Horde2D = root.get_node("Horde2D")
	var player: Node2D = root.get_node("Player")
	assert_eq(horde.target, player, "the target NodePath in the .tscn resolves to the node")
	assert_eq(horde.get_agent_count(), 10000)
	var probe := horde.query_circle(Vector2.ZERO, 5000.0)
	assert_eq(probe.size(), 10000)
	await wait_process_frames(30)
	assert_eq(horde.get_agent_count(), 10000, "population stays constant")
	# The player keeps moving, so check headings instead of distances.
	var heading_to_player := 0
	for i in range(0, probe.size(), 100):
		var id := probe[i]
		var to_player := player.position - horde.get_agent_position(id)
		if horde.get_agent_velocity(id).dot(to_player) > 0.0:
			heading_to_player += 1
	assert_gt(heading_to_player, 90, "almost every sampled agent heads to the player")
	assert_gt(horde.get_debug_stats()["update_usec"], 0)
