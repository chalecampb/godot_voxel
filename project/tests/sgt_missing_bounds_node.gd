@tool
extends VoxelGraphScriptNode


func _init() -> void:
	add_input("x")
	add_input("z")
	add_output("height")


func generate(inputs: Dictionary, outputs: Dictionary) -> void:
	outputs["height"] = inputs["x"] + inputs["z"]
