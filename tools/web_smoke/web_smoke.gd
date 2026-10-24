extends Node2D
## Web smoke test: loads the extension, simulates, queries, and reports to the page.

# Untyped on purpose: the script must still parse if the extension fails to load.
var horde = null
var frames := 0
var died := 0


func _ready() -> void:
	var has_classes := ClassDB.class_exists("Horde2D") and ClassDB.class_exists("HordeType")
	print("HORDE_WEB extensions=", GDExtensionManager.get_loaded_extensions(), " classes=", has_classes)
	if not has_classes:
		_report("FAIL no classes")
		return
	horde = ClassDB.instantiate("Horde2D")
	add_child(horde)
	horde.target_position = Vector2(576, 324)
	var t = ClassDB.instantiate("HordeType")
	t.radius = 5.0
	t.modulate = Color(1, 0.5, 0.4)
	horde.spawn_ring(t, Vector2(576, 324), 300.0, 5000)
	horde.agents_died.connect(func(ids, _p): died += ids.size())


func _process(_delta: float) -> void:
	if horde == null:
		return
	frames += 1
	if frames == 120:
		var hit: PackedInt32Array = horde.damage_circle(Vector2(576, 324), 150.0, 1000.0)
		var stats: Dictionary = horde.get_debug_stats()
		await get_tree().process_frame
		var msg := "OK agents=%d hit=%d died=%d update_ms=%.2f threads=%s" % [horde.get_agent_count(), hit.size(), died, stats["update_usec"] / 1000.0, OS.has_feature("threads")]
		_report(msg)


func _report(msg: String) -> void:
	print("HORDE_WEB_RESULT ", msg)
	if OS.has_feature("web"):
		JavaScriptBridge.eval("window.hordeResult = %s;" % JSON.stringify(msg))
