extends SceneTree
## Exits with code 0 only if the horde GDExtension is loaded and its classes exist.
## Usage: godot --headless --path project --script res://tests/check_extension_loads.gd


func _init() -> void:
	var loaded := GDExtensionManager.get_loaded_extensions()
	var ok := loaded.has("res://addons/horde/horde.gdextension") and ClassDB.class_exists("Horde2D") and ClassDB.class_exists("HordeType")
	print("Godot %s | loaded extensions: %s | classes registered: %s" % [Engine.get_version_info().string, loaded, ok])
	quit(0 if ok else 1)
