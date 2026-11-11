@tool
extends VoxelGraphScriptNode

@export var amplitude := 1.0
@export var bounds_mode := 0


func _init() -> void:
	add_input("x")
	add_input("z")
	add_output("height")
	set_shader_path("res://tests/sgt_test_node.glsl")


func generate(inputs: Dictionary, outputs: Dictionary) -> void:
	if bounds_mode == 6:
		outputs["height"] = INF
	else:
		outputs["height"] = amplitude * (sin(inputs["x"]) + cos(inputs["z"]))


func get_output_bounds() -> Dictionary:
	var bound: float = 2.0 * absf(amplitude)
	if bounds_mode == 1:
		return {}
	if bounds_mode == 2:
		return {"height": Vector2(2.0, -2.0)}
	if bounds_mode == 3:
		return {"height": Vector2(NAN, 1.0)}
	if bounds_mode == 4:
		return {"height": "invalid"}
	if bounds_mode == 5:
		return {"height": Vector2(-0.5, 0.5)}
	return {"height": Vector2(-bound, bound)}
