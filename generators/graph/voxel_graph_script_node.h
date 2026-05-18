#ifndef VOXEL_GRAPH_SCRIPT_NODE_H
#define VOXEL_GRAPH_SCRIPT_NODE_H

#include "../../util/containers/std_vector.h"
#include "../../util/containers/span.h"
#include "../../util/godot/classes/ref_counted.h"
#include "../../util/godot/classes/resource.h"
#include "../../util/godot/core/dictionary.h"
#include "../../util/godot/core/packed_string_array.h"
#include "../../util/godot/core/typed_array.h"
#include "../../util/godot/core/variant.h"
#include "../../util/string/std_string.h"
#include "program_graph.h"

namespace zylann::voxel::pg {

class VoxelGraphScriptNodePort : public Resource {
	GDCLASS(VoxelGraphScriptNodePort, Resource)
public:
	String get_port_name() const;
	void set_port_name(String name);

	int get_port_type() const;
	void set_port_type(int type);

	Variant get_default_value() const;
	void set_default_value(Variant value);

private:
	static void _bind_methods();

	String _name;
	Variant::Type _type = Variant::FLOAT;
	Variant _default_value = 0.f;
};

class VoxelGraphScriptNodeParameter : public Resource {
	GDCLASS(VoxelGraphScriptNodeParameter, Resource)
public:
	String get_parameter_name() const;
	void set_parameter_name(String name);

	int get_parameter_type() const;
	void set_parameter_type(int type);

	Variant get_default_value() const;
	void set_default_value(Variant value);

private:
	static void _bind_methods();

	String _name;
	Variant::Type _type = Variant::FLOAT;
	Variant _default_value = 0.f;
};

class VoxelGraphScriptNode : public Resource {
	GDCLASS(VoxelGraphScriptNode, Resource)
public:
	static const char *SGN_SCRIPT_PROPERTY_NAME;

	struct ShaderSource {
		bool success = false;
		String error;
		StdString function_name;
		StdString source_code;
	};

	void set_script_path(String path);
	String get_script_path() const;
	void set_attached_script(Variant script);
	Variant get_attached_script() const;
	bool reload_attached_script();
	bool reload_script_contract(String path);
	void refresh_metadata();
	void update_graph_node_layout(
			ProgramGraph &graph,
			uint32_t node_id,
			StdVector<ProgramGraph::Connection> *removed_connections
	);

	void set_shader_path(String path);
	String get_shader_path() const;

	void set_entry_point(String entry_point);
	String get_entry_point() const;

	TypedArray<VoxelGraphScriptNodePort> get_inputs() const;
	void set_inputs(TypedArray<VoxelGraphScriptNodePort> inputs);
	void clear_inputs();
	Ref<VoxelGraphScriptNodePort> add_input(String name, int type = Variant::FLOAT);
	Span<const Ref<VoxelGraphScriptNodePort>> get_input_ports() const;

	TypedArray<VoxelGraphScriptNodePort> get_outputs() const;
	void set_outputs(TypedArray<VoxelGraphScriptNodePort> outputs);
	void clear_outputs();
	Ref<VoxelGraphScriptNodePort> add_output(String name, int type = Variant::FLOAT);
	Span<const Ref<VoxelGraphScriptNodePort>> get_output_ports() const;

	TypedArray<VoxelGraphScriptNodeParameter> get_parameters() const;
	void set_parameters(TypedArray<VoxelGraphScriptNodeParameter> parameters);
	Span<const Ref<VoxelGraphScriptNodeParameter>> get_parameter_definitions() const;
	void set_parameter_value(String name, Variant value);
	Variant get_parameter_value(String name) const;

	bool validate();

	PackedStringArray get_validation_errors() const;
	PackedStringArray get_validation_warnings() const;
	bool is_valid() const;
	bool is_gpu_compatible() const;
	uint64_t get_revision() const;
	ShaderSource get_shader_source(uint32_t node_id);
	void generate(Dictionary inputs, Dictionary outputs);

#ifdef TOOLS_ENABLED
	void get_configuration_warnings(PackedStringArray &out_warnings) const;
#endif

private:
	struct NamedType {
		String name;
		Variant::Type type = Variant::NIL;
		Variant default_value;
	};

	static void _bind_methods();

	void validate_metadata();
	bool validate(bool refresh_metadata_from_script, bool force = false);
	void validate_script_contract(bool refresh_metadata_from_script);
	void validate_glsl();
	void apply_graph_node_layout(ProgramGraph::Node &node);
	void copy_contract_from_script_instance(const VoxelGraphScriptNode &script_instance);
	StdVector<NamedType> get_exported_parameters_from_property_list();
	void sync_parameters_from_script_contract(const StdVector<NamedType> &parameters);
	void validate_name_matches(
			Span<const NamedType> actual,
			Span<const Ref<VoxelGraphScriptNodeParameter>> expected,
			const String &actual_label,
			bool missing_actual_is_error
	);
	void register_child_resources();
	void unregister_child_resources();
	void _on_child_resource_changed();
	void clear_contract(String script_path, String error_message);
	void emit_changed_deferred();
	void set_change_notifications_suppressed(bool suppressed);
	void clear_validation();
	void add_error(String message);
	void add_warning(String message);
	void increment_revision();

	String _script_path;
	String _shader_path;
	String _entry_point = "generate";
	StdVector<Ref<VoxelGraphScriptNodePort>> _inputs;
	StdVector<Ref<VoxelGraphScriptNodePort>> _outputs;
	StdVector<Ref<VoxelGraphScriptNodeParameter>> _parameters;

	PackedStringArray _errors;
	PackedStringArray _warnings;
	bool _valid = false;
	bool _gpu_compatible = false;
	bool _suppress_change_notifications = false;
	bool _has_deferred_change_notification = false;
	uint64_t _revision = 0;
	uint64_t _validated_revision = uint64_t(-1);

};

} // namespace zylann::voxel::pg

#endif // VOXEL_GRAPH_SCRIPT_NODE_H
