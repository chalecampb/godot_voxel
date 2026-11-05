extends SceneTree


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var includes := [
		"test_voxel_graph_sgn_load_script_contract",
		"test_voxel_graph_sgn_clear_script_reverts_ports",
		"test_voxel_graph_sgn_child_resource_change_invalidates_validation",
		"test_voxel_graph_sgn_shader_path_property",
		"test_voxel_graph_sgn_shader_compilation",
		"test_voxel_graph_sgn_shader_project_file_validation",
		"test_voxel_graph_script_node_contract",
		"test_voxel_graph_script_node_cpu_execution",
		"test_voxel_graph_script_node_debug_compile_does_not_emit_changed",
		"test_voxel_graph_script_node_port_refresh",
		"test_voxel_graph_script_node_copy_keeps_connections_after_reload",
		"test_voxel_graph_script_texture_2d_contract_execution",
		"test_voxel_graph_script_texture_2d_rejects_invalid_bounds",
		"test_voxel_graph_script_texture_2d_clamps_and_rebuilds_snapshot_bounds",
		"test_voxel_graph_script_texture_2d_rejects_missing_contract",
		"test_voxel_graph_script_texture_2d_persistence_keeps_dynamic_connections",
		"test_voxel_graph_script_texture_2d_bounds_enable_broad_block_culling",
	]
	includes.append("test_voxel_graph_sgn_inspector_hides_validation_properties")
	includes.append("test_voxel_graph_editor_create_dynamic_layout_node")
	if OS.get_cmdline_user_args().has("--gpu"):
		includes.append("test_voxel_graph_script_texture_2d_gpu_shader_compiles")
	VoxelEngine.run_tests({"includes": includes})
	print("SCRIPT_GRAPH_TESTS_PASSED")
	quit(0)
