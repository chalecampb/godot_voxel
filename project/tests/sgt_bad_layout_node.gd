@tool
extends VoxelGraphScriptNode


func _init() -> void:
	add_input("z")
	add_input("x")
	add_output("height")


func generate(inputs: Dictionary, outputs: Dictionary) -> void:
	outputs["height"] = inputs["x"] + inputs["z"]


func get_output_bounds() -> Dictionary:
	return {"height": Vector2(-2.0, 2.0)}
