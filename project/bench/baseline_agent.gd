extends CharacterBody2D
## GDScript baseline agent: the usual "one node per enemy" approach.
## Same behaviour budget as a Horde2D agent: seek the target with bounded acceleration
## and max speed; bodies collide with each other through move_and_slide().

var target: Node2D
var max_speed := 90.0
var acceleration := 600.0


func _physics_process(delta: float) -> void:
	var to_target := target.global_position - global_position
	var desired := Vector2.ZERO
	if to_target.length_squared() > 1.0:
		desired = to_target.normalized() * max_speed
	velocity = velocity.move_toward(desired, acceleration * delta)
	move_and_slide()
