@tool
extends VoxelGraphScriptNode

@export var gain := 2.0


func _init() -> void:
	add_input("x")
	add_input("bias")
	add_output("sdf")
	set_shader_path("res://tests/sgn_bad_shader.glsl")


func generate(inputs: Dictionary, outputs: Dictionary) -> void:
	outputs["sdf"] = inputs["x"] * gain + inputs["bias"]
