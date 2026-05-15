#include "voxel_graph_script_node.h"

#include "../../constants/voxel_string_names.h"
#include "../../util/godot/classes/engine.h"
#include "../../util/godot/classes/file_access.h"
#include "../../util/godot/classes/object.h"
#include "../../util/godot/classes/resource_loader.h"
#include "../../util/godot/classes/script.h"
#include "../../util/godot/core/callable_mp.h"
#include "../../util/godot/core/class_db.h"
#include "../../util/godot/core/string.h"
#include "../../util/io/log.h"
#include "../../util/macros.h"
#include "../../util/string/format.h"

#include <regex>

namespace zylann::voxel::pg {

const char *VoxelGraphScriptNode::SGN_SCRIPT_PROPERTY_NAME = "SGN Script";

namespace {

bool is_supported_parameter_type(Variant::Type type) {
	return type == Variant::FLOAT || type == Variant::INT || type == Variant::BOOL;
}

bool is_supported_port_type(Variant::Type type) {
	return type == Variant::FLOAT;
}

String type_to_string(Variant::Type type) {
	switch (type) {
		case Variant::FLOAT:
			return "float";
		case Variant::INT:
			return "int";
		case Variant::BOOL:
			return "bool";
		default:
			return Variant::get_type_name(type);
	}
}

Variant::Type string_to_supported_type(const std::string &s) {
	if (s == "float" || s == "TYPE_FLOAT" || s == "Variant.FLOAT" || s == "Variant::FLOAT") {
		return Variant::FLOAT;
	}
	if (s == "int" || s == "TYPE_INT" || s == "Variant.INT" || s == "Variant::INT") {
		return Variant::INT;
	}
	if (s == "bool" || s == "TYPE_BOOL" || s == "Variant.BOOL" || s == "Variant::BOOL") {
		return Variant::BOOL;
	}
	return Variant::NIL;
}

std::string trim(std::string s) {
	const size_t b = s.find_first_not_of(" \t\r\n");
	if (b == std::string::npos) {
		return std::string();
	}
	const size_t e = s.find_last_not_of(" \t\r\n");
	return s.substr(b, e - b + 1);
}

bool read_text_file(String path, String &out_text, String &out_error) {
	if (path.is_empty()) {
		out_error = "Path is empty.";
		return false;
	}

	Error err;
	Ref<FileAccess> f = zylann::godot::open_file(path, FileAccess::READ, err);
	if (err != OK || f.is_null()) {
		out_error = String("Could not open `{0}`.").format(varray(path));
		return false;
	}

	out_text = zylann::godot::get_as_text(**f);
	return true;
}

void push_unique_name_error(
		PackedStringArray &errors,
		const String &kind,
		const String &name,
		StdVector<String> &known_names
) {
	if (name.is_empty()) {
		errors.append(String("{0} name cannot be empty.").format(varray(kind)));
		return;
	}
	for (const String &known_name : known_names) {
		if (known_name == name) {
			errors.append(String("Duplicate {0} name `{1}`.").format(varray(kind, name)));
			return;
		}
	}
	known_names.push_back(name);
}

void push_required_name_error(PackedStringArray &errors, const String &kind, const String &name) {
	if (name.is_empty()) {
		errors.append(String("{0} name cannot be empty.").format(varray(kind)));
	}
}

StdString get_dynamic_port_name(const ProgramGraph::Port &port) {
	return port.dynamic_name;
}

} // namespace

String VoxelGraphScriptNodePort::get_port_name() const {
	if (!_name.is_empty()) {
		return _name;
	}
	return get_name();
}

void VoxelGraphScriptNodePort::set_port_name(String p_name) {
	_name = p_name;
	set_name(p_name);
	emit_changed();
}

int VoxelGraphScriptNodePort::get_port_type() const {
	return _type;
}

void VoxelGraphScriptNodePort::set_port_type(int type) {
	_type = static_cast<Variant::Type>(type);
	emit_changed();
}

Variant VoxelGraphScriptNodePort::get_default_value() const {
	return _default_value;
}

void VoxelGraphScriptNodePort::set_default_value(Variant value) {
	_default_value = value;
	emit_changed();
}

void VoxelGraphScriptNodePort::_bind_methods() {
	using Self = VoxelGraphScriptNodePort;

	ClassDB::bind_method(D_METHOD("set_port_name", "name"), &Self::set_port_name);
	ClassDB::bind_method(D_METHOD("get_port_name"), &Self::get_port_name);
	ClassDB::bind_method(D_METHOD("set_type", "type"), &Self::set_port_type);
	ClassDB::bind_method(D_METHOD("get_type"), &Self::get_port_type);
	ClassDB::bind_method(D_METHOD("set_default_value", "value"), &Self::set_default_value);
	ClassDB::bind_method(D_METHOD("get_default_value"), &Self::get_default_value);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "name"), "set_port_name", "get_port_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "type", PROPERTY_HINT_ENUM, "Nil,Bool,Int,Float"), "set_type", "get_type");
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "default_value"), "set_default_value", "get_default_value");
}

String VoxelGraphScriptNodeParameter::get_parameter_name() const {
	if (!_name.is_empty()) {
		return _name;
	}
	return get_name();
}

void VoxelGraphScriptNodeParameter::set_parameter_name(String p_name) {
	_name = p_name;
	set_name(p_name);
	emit_changed();
}

int VoxelGraphScriptNodeParameter::get_parameter_type() const {
	return _type;
}

void VoxelGraphScriptNodeParameter::set_parameter_type(int type) {
	_type = static_cast<Variant::Type>(type);
	emit_changed();
}

Variant VoxelGraphScriptNodeParameter::get_default_value() const {
	return _default_value;
}

void VoxelGraphScriptNodeParameter::set_default_value(Variant value) {
	_default_value = value;
	emit_changed();
}

void VoxelGraphScriptNodeParameter::_bind_methods() {
	using Self = VoxelGraphScriptNodeParameter;

	ClassDB::bind_method(D_METHOD("set_parameter_name", "name"), &Self::set_parameter_name);
	ClassDB::bind_method(D_METHOD("get_parameter_name"), &Self::get_parameter_name);
	ClassDB::bind_method(D_METHOD("set_type", "type"), &Self::set_parameter_type);
	ClassDB::bind_method(D_METHOD("get_type"), &Self::get_parameter_type);
	ClassDB::bind_method(D_METHOD("set_default_value", "value"), &Self::set_default_value);
	ClassDB::bind_method(D_METHOD("get_default_value"), &Self::get_default_value);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "name"), "set_parameter_name", "get_parameter_name");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "type", PROPERTY_HINT_ENUM, "Nil,Bool,Int,Float"), "set_type", "get_type");
	ADD_PROPERTY(PropertyInfo(Variant::NIL, "default_value"), "set_default_value", "get_default_value");
}

void VoxelGraphScriptNode::register_child_resources() {
	const Callable callable = callable_mp(this, &VoxelGraphScriptNode::_on_child_resource_changed);
	const StringName changed = VoxelStringNames::get_singleton().changed;

	auto register_ports = [&callable, &changed](Span<const Ref<VoxelGraphScriptNodePort>> ports) {
		for (const Ref<VoxelGraphScriptNodePort> &port : ports) {
			if (port.is_valid() && !port->is_connected(changed, callable)) {
				port->connect(changed, callable);
			}
		}
	};

	register_ports(to_span(_inputs));
	register_ports(to_span(_outputs));

	for (const Ref<VoxelGraphScriptNodeParameter> &parameter : _parameters) {
		if (parameter.is_valid() && !parameter->is_connected(changed, callable)) {
			parameter->connect(changed, callable);
		}
	}
}

void VoxelGraphScriptNode::update_graph_node_layout(
		ProgramGraph &graph,
		uint32_t node_id,
		StdVector<ProgramGraph::Connection> *removed_connections
) {
	ProgramGraph::Node &node = graph.get_node(node_id);

	struct InputConnection {
		StdString name;
		ProgramGraph::PortLocation src;
		bool connected = false;
	};
	struct OutputConnection {
		StdString name;
		ProgramGraph::PortLocation dst;
	};

	StdVector<InputConnection> old_inputs;
	for (uint32_t input_index = 0; input_index < node.inputs.size(); ++input_index) {
		InputConnection input;
		input.name = get_dynamic_port_name(node.inputs[input_index]);
		if (node.inputs[input_index].connections.size() > 0) {
			ZN_ASSERT(node.inputs[input_index].connections.size() == 1);
			input.src = node.inputs[input_index].connections[0];
			input.connected = true;
			graph.disconnect(input.src, { node_id, input_index });
			if (removed_connections != nullptr) {
				removed_connections->push_back({ input.src, { node_id, input_index } });
			}
		}
		old_inputs.push_back(input);
	}

	StdVector<OutputConnection> old_outputs;
	for (uint32_t output_index = 0; output_index < node.outputs.size(); ++output_index) {
		const StdString output_name = get_dynamic_port_name(node.outputs[output_index]);
		const StdVector<ProgramGraph::PortLocation> destinations = node.outputs[output_index].connections;
		for (ProgramGraph::PortLocation dst : destinations) {
			graph.disconnect({ node_id, output_index }, dst);
			if (removed_connections != nullptr) {
				removed_connections->push_back({ { node_id, output_index }, dst });
			}
			old_outputs.push_back({ output_name, dst });
		}
	}

	apply_graph_node_layout(node);

	for (uint32_t input_index = 0; input_index < node.inputs.size(); ++input_index) {
		const StdString input_name = get_dynamic_port_name(node.inputs[input_index]);
		for (const InputConnection &old_input : old_inputs) {
			if (old_input.name == input_name) {
				if (old_input.connected && graph.is_output_port_valid(old_input.src) &&
						graph.can_connect(old_input.src, { node_id, input_index })) {
					graph.connect(old_input.src, { node_id, input_index });
				}
				break;
			}
		}
	}

	for (uint32_t output_index = 0; output_index < node.outputs.size(); ++output_index) {
		const StdString output_name = get_dynamic_port_name(node.outputs[output_index]);
		for (const OutputConnection &old_output : old_outputs) {
			if (old_output.name == output_name && graph.is_input_port_valid(old_output.dst) &&
					graph.can_connect({ node_id, output_index }, old_output.dst)) {
				graph.connect({ node_id, output_index }, old_output.dst);
			}
		}
	}
}

void VoxelGraphScriptNode::apply_graph_node_layout(ProgramGraph::Node &node) {
	if (get_script_path().is_empty()) {
		node.inputs.clear();
		node.outputs.clear();
		node.default_inputs.clear();
		node.autoconnect_default_inputs = false;
		return;
	}
	if (!validate()) {
		PackedStringArray errors = get_validation_errors();
		for (int i = 0; i < errors.size(); ++i) {
			ERR_PRINT(errors[i]);
		}
		node.inputs.clear();
		node.outputs.clear();
		node.default_inputs.clear();
		node.autoconnect_default_inputs = false;
		return;
	}

	auto set_ports = [](StdVector<ProgramGraph::Port> &ports, Span<const Ref<VoxelGraphScriptNodePort>> script_ports) {
		ports.clear();
		for (const Ref<VoxelGraphScriptNodePort> &script_port : script_ports) {
			ProgramGraph::Port port;
			if (script_port.is_valid()) {
				String name = script_port->get_port_name();
				if (name.is_empty()) {
					name = "<unnamed>";
				}
				const CharString name_utf8 = name.utf8();
				port.dynamic_name = name_utf8.get_data();
			} else {
				port.dynamic_name = "<unnamed>";
			}
			ports.push_back(port);
		}
	};

	set_ports(node.inputs, get_input_ports());
	set_ports(node.outputs, get_output_ports());
	node.default_inputs.resize(node.inputs.size());
	const Span<const Ref<VoxelGraphScriptNodePort>> input_ports = get_input_ports();
	for (unsigned int i = 0; i < node.default_inputs.size(); ++i) {
		if (i < input_ports.size() && input_ports[i].is_valid()) {
			node.default_inputs[i] = input_ports[i]->get_default_value();
		} else {
			node.default_inputs[i] = 0.f;
		}
	}
	node.autoconnect_default_inputs = false;
}

void VoxelGraphScriptNode::unregister_child_resources() {
	const Callable callable = callable_mp(this, &VoxelGraphScriptNode::_on_child_resource_changed);
	const StringName changed = VoxelStringNames::get_singleton().changed;

	auto unregister_ports = [&callable, &changed](Span<const Ref<VoxelGraphScriptNodePort>> ports) {
		for (const Ref<VoxelGraphScriptNodePort> &port : ports) {
			if (port.is_valid() && port->is_connected(changed, callable)) {
				port->disconnect(changed, callable);
			}
		}
	};

	unregister_ports(to_span(_inputs));
	unregister_ports(to_span(_outputs));

	for (const Ref<VoxelGraphScriptNodeParameter> &parameter : _parameters) {
		if (parameter.is_valid() && parameter->is_connected(changed, callable)) {
			parameter->disconnect(changed, callable);
		}
	}
}

void VoxelGraphScriptNode::_on_child_resource_changed() {
	emit_changed_deferred();
}

void VoxelGraphScriptNode::clear_contract(String script_path, String error_message) {
	set_script(Variant());
	_script_path = script_path;
	unregister_child_resources();
	_inputs.clear();
	_outputs.clear();
	_parameters.clear();
	register_child_resources();
	clear_validation();
	if (!error_message.is_empty()) {
		add_error(error_message);
	}
	increment_revision();
	emit_changed_deferred();
}

void VoxelGraphScriptNode::emit_changed_deferred() {
	if (_suppress_change_notifications) {
		_has_deferred_change_notification = true;
		return;
	}
	emit_changed();
}

void VoxelGraphScriptNode::set_change_notifications_suppressed(bool suppressed) {
	if (_suppress_change_notifications == suppressed) {
		return;
	}

	_suppress_change_notifications = suppressed;
	if (!_suppress_change_notifications && _has_deferred_change_notification) {
		_has_deferred_change_notification = false;
		emit_changed();
	}
}

void VoxelGraphScriptNode::set_script_path(String path) {
	_script_path = path;
	if (path.is_empty()) {
		set_attached_script(Variant());
		return;
	}
	reload_script_contract(path);
	emit_changed_deferred();
}

String VoxelGraphScriptNode::get_script_path() const {
	Variant script_variant = get_script();
	Ref<Script> script = script_variant;
	if (script.is_valid() && !script->get_path().is_empty()) {
		return script->get_path();
	}
	return _script_path;
}

void VoxelGraphScriptNode::set_attached_script(Variant script) {
	Ref<Script> script_ref = script;
	if (script_ref.is_null()) {
		clear_contract(String(), String());
		return;
	}
	reload_script_contract(script_ref->get_path());
}

bool VoxelGraphScriptNode::reload_script_contract(String script_path) {
	if (script_path.is_empty()) {
		add_error("GDScript path is empty.");
		return false;
	}

	Ref<Resource> resource = zylann::godot::reload_resource(script_path);
	Ref<Script> script = resource;

	if (script.is_null()) {
		clear_contract(script_path, String("Could not load GDScript `{0}`.").format(varray(script_path)));
		return false;
	}

	Variant script_node_instance_variant = script->call("new");
	Ref<VoxelGraphScriptNode> script_node_instance = script_node_instance_variant;
	if (script_node_instance.is_null()) {
		clear_contract(script_path, String("GDScript `{0}` must extend VoxelGraphScriptNode.").format(varray(script_path)));
		return false;
	}

	set_change_notifications_suppressed(true);
	unregister_child_resources();
	_inputs.clear();
	_outputs.clear();
	register_child_resources();
	set_script(Variant());
	_script_path = script_path;
	set_script(script);
	copy_contract_from_script_instance(**script_node_instance);
	increment_revision();
	validate(true);
	_has_deferred_change_notification = true;
	set_change_notifications_suppressed(false);
	return _valid;
}

Variant VoxelGraphScriptNode::get_attached_script() const {
	return get_script();
}

bool VoxelGraphScriptNode::reload_attached_script() {
	const String script_path = get_script_path();
	if (script_path.is_empty()) {
		return false;
	}
	return reload_script_contract(script_path);
}

void VoxelGraphScriptNode::refresh_metadata() {
	set_change_notifications_suppressed(true);
	increment_revision();
	validate(true);
	_has_deferred_change_notification = true;
	set_change_notifications_suppressed(false);
}

void VoxelGraphScriptNode::set_shader_path(String path) {
	_shader_path = path;
	increment_revision();
	validate();
	emit_changed_deferred();
}

String VoxelGraphScriptNode::get_shader_path() const {
	return _shader_path;
}

void VoxelGraphScriptNode::set_entry_point(String entry_point) {
	_entry_point = entry_point;
	increment_revision();
	validate();
	emit_changed_deferred();
}

String VoxelGraphScriptNode::get_entry_point() const {
	return _entry_point;
}

TypedArray<VoxelGraphScriptNodePort> VoxelGraphScriptNode::get_inputs() const {
	return zylann::godot::to_typed_array(to_span(_inputs));
}

void VoxelGraphScriptNode::set_inputs(TypedArray<VoxelGraphScriptNodePort> inputs) {
	unregister_child_resources();
	zylann::godot::copy_to(_inputs, inputs);
	register_child_resources();
	increment_revision();
	emit_changed_deferred();
}

void VoxelGraphScriptNode::clear_inputs() {
	unregister_child_resources();
	_inputs.clear();
	register_child_resources();
	increment_revision();
	emit_changed_deferred();
}

Ref<VoxelGraphScriptNodePort> VoxelGraphScriptNode::add_input(String p_name, int type) {
	Ref<VoxelGraphScriptNodePort> port;
	port.instantiate();
	port->set_port_name(p_name);
	port->set_port_type(type);
	_inputs.push_back(port);
	register_child_resources();
	increment_revision();
	emit_changed_deferred();
	return port;
}

Span<const Ref<VoxelGraphScriptNodePort>> VoxelGraphScriptNode::get_input_ports() const {
	return to_span(_inputs);
}

TypedArray<VoxelGraphScriptNodePort> VoxelGraphScriptNode::get_outputs() const {
	return zylann::godot::to_typed_array(to_span(_outputs));
}

void VoxelGraphScriptNode::set_outputs(TypedArray<VoxelGraphScriptNodePort> outputs) {
	unregister_child_resources();
	zylann::godot::copy_to(_outputs, outputs);
	register_child_resources();
	increment_revision();
	emit_changed_deferred();
}

void VoxelGraphScriptNode::clear_outputs() {
	unregister_child_resources();
	_outputs.clear();
	register_child_resources();
	increment_revision();
	emit_changed_deferred();
}

Ref<VoxelGraphScriptNodePort> VoxelGraphScriptNode::add_output(String p_name, int type) {
	Ref<VoxelGraphScriptNodePort> port;
	port.instantiate();
	port->set_port_name(p_name);
	port->set_port_type(type);
	_outputs.push_back(port);
	register_child_resources();
	increment_revision();
	emit_changed_deferred();
	return port;
}

Span<const Ref<VoxelGraphScriptNodePort>> VoxelGraphScriptNode::get_output_ports() const {
	return to_span(_outputs);
}

TypedArray<VoxelGraphScriptNodeParameter> VoxelGraphScriptNode::get_parameters() const {
	return zylann::godot::to_typed_array(to_span(_parameters));
}

void VoxelGraphScriptNode::set_parameters(TypedArray<VoxelGraphScriptNodeParameter> parameters) {
	unregister_child_resources();
	zylann::godot::copy_to(_parameters, parameters);
	register_child_resources();
	increment_revision();
	emit_changed_deferred();
}

Span<const Ref<VoxelGraphScriptNodeParameter>> VoxelGraphScriptNode::get_parameter_definitions() const {
	return to_span(_parameters);
}

void VoxelGraphScriptNode::set_parameter_value(String p_name, Variant value) {
	set(StringName(p_name), value);
	bool found = false;
	for (Ref<VoxelGraphScriptNodeParameter> &parameter : _parameters) {
		if (parameter.is_valid() && parameter->get_parameter_name() == p_name) {
			parameter->set_default_value(value);
			found = true;
		}
	}
	if (found) {
		increment_revision();
		emit_changed_deferred();
	}
}

Variant VoxelGraphScriptNode::get_parameter_value(String p_name) const {
	return get(StringName(p_name));
}

PackedStringArray VoxelGraphScriptNode::get_validation_errors() const {
	return _errors;
}

PackedStringArray VoxelGraphScriptNode::get_validation_warnings() const {
	return _warnings;
}

bool VoxelGraphScriptNode::is_valid() const {
	return _valid;
}

bool VoxelGraphScriptNode::is_gpu_compatible() const {
	return _gpu_compatible;
}

uint64_t VoxelGraphScriptNode::get_revision() const {
	return _revision;
}

void VoxelGraphScriptNode::generate(Dictionary inputs, Dictionary outputs) {
	ERR_FAIL_COND_MSG(_entry_point.is_empty(), "ScriptGraphNode entry point name cannot be empty.");
	const StringName entry_point(_entry_point);
	ERR_FAIL_COND_MSG(
			!has_method(entry_point),
			String("ScriptGraphNode entry point `{0}` was not found.").format(varray(_entry_point))
	);
	call(entry_point, inputs, outputs);
}

void VoxelGraphScriptNode::clear_validation() {
	_errors.clear();
	_warnings.clear();
	_valid = false;
	_gpu_compatible = false;
}

void VoxelGraphScriptNode::add_error(String message) {
	_errors.append(message);
}

void VoxelGraphScriptNode::add_warning(String message) {
	_warnings.append(message);
}

void VoxelGraphScriptNode::increment_revision() {
	++_revision;
}

void VoxelGraphScriptNode::validate_metadata() {
	if (_entry_point.is_empty()) {
		add_error("Entry point name cannot be empty.");
	}

	StdVector<String> port_names;
	for (const Ref<VoxelGraphScriptNodePort> &port : _inputs) {
		if (port.is_null()) {
			add_error("Input list contains a null port.");
			continue;
		}
		push_unique_name_error(_errors, "input", port->get_port_name(), port_names);
		if (!is_supported_port_type(static_cast<Variant::Type>(port->get_port_type()))) {
			add_error(String("Input `{0}` has unsupported type `{1}`. First version graph ports must be float.")
							  .format(varray(port->get_port_name(), type_to_string(static_cast<Variant::Type>(port->get_port_type())))));
		}
	}

	port_names.clear();
	if (_outputs.size() == 0) {
		add_error("ScriptGraphNode must define at least one output.");
	}
	for (const Ref<VoxelGraphScriptNodePort> &port : _outputs) {
		if (port.is_null()) {
			add_error("Output list contains a null port.");
			continue;
		}
		push_unique_name_error(_errors, "output", port->get_port_name(), port_names);
		if (!is_supported_port_type(static_cast<Variant::Type>(port->get_port_type()))) {
			add_error(String("Output `{0}` has unsupported type `{1}`. First version graph ports must be float.")
							  .format(varray(port->get_port_name(), type_to_string(static_cast<Variant::Type>(port->get_port_type())))));
		}
	}

	for (const Ref<VoxelGraphScriptNodeParameter> &parameter : _parameters) {
		if (parameter.is_null()) {
			add_error("Parameter list contains a null parameter.");
			continue;
		}
		push_required_name_error(_errors, "parameter", parameter->get_parameter_name());
		if (!is_supported_parameter_type(static_cast<Variant::Type>(parameter->get_parameter_type()))) {
			add_error(String("Parameter `{0}` has unsupported type `{1}`.")
							  .format(varray(parameter->get_parameter_name(),
									  type_to_string(static_cast<Variant::Type>(parameter->get_parameter_type())))));
		}
	}
}

void VoxelGraphScriptNode::validate_name_matches(
		Span<const NamedType> actual,
		Span<const Ref<VoxelGraphScriptNodeParameter>> expected,
		const String &actual_label,
		bool missing_actual_is_error
) {
	for (const Ref<VoxelGraphScriptNodeParameter> &parameter : expected) {
		if (parameter.is_null()) {
			continue;
		}
		bool found = false;
		for (const NamedType &actual_item : actual) {
			if (actual_item.name == parameter->get_parameter_name()) {
				found = true;
				const Variant::Type expected_type = static_cast<Variant::Type>(parameter->get_parameter_type());
				if (actual_item.type != expected_type) {
					const String message = String("{0} `{1}` has type `{2}`, expected `{3}`.")
												   .format(varray(actual_label,
														   actual_item.name,
														   type_to_string(actual_item.type),
														   type_to_string(expected_type)));
					if (missing_actual_is_error) {
						add_error(message);
					} else {
						add_warning(message);
					}
				}
			}
		}
		if (!found) {
			const String message = String("{0} missing metadata parameter `{1}`.")
										   .format(varray(actual_label, parameter->get_parameter_name()));
			if (missing_actual_is_error) {
				add_error(message);
			} else {
				add_warning(message);
			}
		}
	}

	for (const NamedType &actual_item : actual) {
		bool found = false;
		for (const Ref<VoxelGraphScriptNodeParameter> &parameter : expected) {
			if (parameter.is_valid() && actual_item.name == parameter->get_parameter_name()) {
				found = true;
				break;
			}
		}
		if (!found) {
			const String message =
					String("{0} `{1}` has no matching metadata parameter.").format(varray(actual_label, actual_item.name));
			if (missing_actual_is_error) {
				add_error(message);
			} else {
				add_warning(message);
			}
		}
	}
}

void VoxelGraphScriptNode::copy_contract_from_script_instance(const VoxelGraphScriptNode &src_script_node) {
	auto copy_ports = [](StdVector<Ref<VoxelGraphScriptNodePort>> &dst,
							  Span<const Ref<VoxelGraphScriptNodePort>> src) {
		dst.clear();
		dst.reserve(src.size());
		for (const Ref<VoxelGraphScriptNodePort> &src_port : src) {
			if (src_port.is_null()) {
				dst.push_back(Ref<VoxelGraphScriptNodePort>());
				continue;
			}

			Ref<VoxelGraphScriptNodePort> dst_port;
			dst_port.instantiate();
			dst_port->set_port_name(src_port->get_port_name());
			dst_port->set_port_type(src_port->get_port_type());
			dst_port->set_default_value(src_port->get_default_value());
			dst.push_back(dst_port);
		}
	};

	unregister_child_resources();
	_shader_path = src_script_node.get_shader_path();
	_entry_point = src_script_node.get_entry_point();
	copy_ports(_inputs, src_script_node.get_input_ports());
	copy_ports(_outputs, src_script_node.get_output_ports());
	register_child_resources();
}

StdVector<VoxelGraphScriptNode::NamedType> VoxelGraphScriptNode::get_exported_parameters_from_property_list() {
	StdVector<NamedType> parameters;

	Ref<Script> script = get_script();
	if (script.is_null()) {
		return parameters;
	}

	StdVector<zylann::godot::PropertyInfoWrapper> properties;
	zylann::godot::get_property_list(*this, properties);
	for (const zylann::godot::PropertyInfoWrapper &property : properties) {
		const uint32_t usage = property.usage;
		if ((usage & PROPERTY_USAGE_SCRIPT_VARIABLE) == 0) {
			continue;
		}
		const String property_name = property.name;
		const Variant::Type type = property.type;
		if (!is_supported_parameter_type(type)) {
			add_error(String("GDScript export `{0}` has unsupported type. Use float, int, or bool.")
							  .format(varray(property_name)));
			continue;
		}

		NamedType item;
		item.name = property_name;
		item.type = type;
		item.default_value = get(property_name);
		parameters.push_back(item);
	}

	return parameters;
}

void VoxelGraphScriptNode::sync_parameters_from_gdscript_contract(const StdVector<NamedType> &parameters) {
	auto find_existing_parameter = [](Span<Ref<VoxelGraphScriptNodeParameter>> existing_parameters,
										   const String &parameter_name,
										   Variant::Type type) -> Ref<VoxelGraphScriptNodeParameter> {
		for (Ref<VoxelGraphScriptNodeParameter> &parameter : existing_parameters) {
			if (parameter.is_valid() && parameter->get_parameter_name() == parameter_name &&
					static_cast<Variant::Type>(parameter->get_parameter_type()) == type) {
				Ref<VoxelGraphScriptNodeParameter> result = parameter;
				parameter = Ref<VoxelGraphScriptNodeParameter>();
				return result;
			}
		}
		return Ref<VoxelGraphScriptNodeParameter>();
	};

	StdVector<Ref<VoxelGraphScriptNodeParameter>> old_parameters = _parameters;
	unregister_child_resources();
	_parameters.clear();
	_parameters.reserve(parameters.size());
	for (const NamedType &item : parameters) {
		if (!is_supported_parameter_type(item.type)) {
			continue;
		}
		Ref<VoxelGraphScriptNodeParameter> parameter = find_existing_parameter(to_span(old_parameters), item.name, item.type);
		if (parameter.is_null()) {
			parameter.instantiate();
			parameter->set_default_value(item.default_value);
		}
		parameter->set_parameter_name(item.name);
		parameter->set_parameter_type(item.type);
		set(StringName(item.name), parameter->get_default_value());
		_parameters.push_back(parameter);
	}
	register_child_resources();
}

void VoxelGraphScriptNode::validate_gdscript(bool refresh_metadata_from_script) {
	Ref<Script> script = get_script();
	if (script.is_null()) {
		if (!_script_path.is_empty()) {
			add_error(String("GDScript `{0}` could not be attached to VoxelGraphScriptNode. Check that it extends VoxelGraphScriptNode and compiles.").format(varray(_script_path)));
		}
		return;
	}

	const StringName entry_point(_entry_point);
	if (!has_method(entry_point)) {
		add_error(String("GDScript entry point `{0}` was not found in `{1}`.").format(varray(_entry_point, get_script_path())));
	}

#ifdef TOOLS_ENABLED
	if (Engine::get_singleton()->is_editor_hint() && !script->is_tool()) {
		add_error(String("GDScript `{0}` must use @tool to run ScriptGraphNode in the editor.")
						  .format(varray(get_script_path())));
	}
#endif

	StdVector<NamedType> exports = get_exported_parameters_from_property_list();
	if (refresh_metadata_from_script) {
		sync_parameters_from_gdscript_contract(exports);
	}
	validate_name_matches(to_span(exports), to_span(_parameters), "GDScript export", true);
}

void VoxelGraphScriptNode::validate_glsl() {
	if (_shader_path.is_empty()) {
		return;
	}

	String text;
	String read_error;
	if (!read_text_file(_shader_path, text, read_error)) {
		add_warning(String("GLSL file error: {0}").format(varray(read_error)));
		return;
	}

	const zylann::StdString source_tmp = zylann::godot::to_std_string(text);
	const std::string source(source_tmp.c_str());

	StdVector<NamedType> uniforms;
	const std::regex uniform_regex("\\buniform\\s+(float|int|bool)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*;");
	for (std::sregex_iterator it(source.begin(), source.end(), uniform_regex); it != std::sregex_iterator(); ++it) {
		const std::smatch match = *it;
		NamedType item;
		item.type = string_to_supported_type(match[1].str());
		item.name = zylann::godot::to_godot(match[2].str());
		uniforms.push_back(item);
	}
	validate_name_matches(to_span(uniforms), to_span(_parameters), "GLSL uniform", false);

	const zylann::StdString entry_tmp = zylann::godot::to_std_string(_entry_point);
	const std::string entry(entry_tmp.c_str());
	const std::regex function_regex("\\bvoid\\s+" + entry + "\\s*\\(([^)]*)\\)");
	std::smatch function_match;
	if (!std::regex_search(source, function_match, function_regex)) {
		add_warning(String("GLSL entry point `{0}` was not found.").format(varray(_entry_point)));
		return;
	}

	StdVector<NamedType> inputs;
	StdVector<NamedType> outputs;
	const std::string params = function_match[1].str();
	size_t start = 0;
	while (start < params.size()) {
		const size_t comma = params.find(',', start);
		const std::string param = trim(params.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
		if (!param.empty()) {
			const std::regex param_regex("^(out\\s+)?(float|int|bool)\\s+([A-Za-z_][A-Za-z0-9_]*)$");
			std::smatch param_match;
			if (std::regex_match(param, param_match, param_regex)) {
				NamedType item;
				item.type = string_to_supported_type(param_match[2].str());
				item.name = zylann::godot::to_godot(param_match[3].str());
				if (param_match[1].matched) {
					outputs.push_back(item);
				} else {
					inputs.push_back(item);
				}
			} else {
				add_warning(String("Unsupported GLSL entry point parameter `{0}`.").format(varray(zylann::godot::to_godot(param))));
			}
		}
		if (comma == std::string::npos) {
			break;
		}
		start = comma + 1;
	}

	if (inputs.size() != _inputs.size()) {
		add_warning(String("GLSL entry point has {0} inputs, expected {1}.").format(varray(inputs.size(), _inputs.size())));
	}
	for (unsigned int i = 0; i < inputs.size() && i < _inputs.size(); ++i) {
		const Ref<VoxelGraphScriptNodePort> port = _inputs[i];
		if (port.is_null()) {
			continue;
		}
		if (inputs[i].name != port->get_port_name() || inputs[i].type != static_cast<Variant::Type>(port->get_port_type())) {
			add_warning(String("GLSL input {0} is `{1}: {2}`, expected `{3}: {4}`.")
								.format(varray(i,
										inputs[i].name,
										type_to_string(inputs[i].type),
										port->get_port_name(),
										type_to_string(static_cast<Variant::Type>(port->get_port_type())))));
		}
	}

	if (outputs.size() != _outputs.size()) {
		add_warning(String("GLSL entry point has {0} outputs, expected {1}.").format(varray(outputs.size(), _outputs.size())));
	}
	for (unsigned int i = 0; i < outputs.size() && i < _outputs.size(); ++i) {
		const Ref<VoxelGraphScriptNodePort> port = _outputs[i];
		if (port.is_null()) {
			continue;
		}
		if (outputs[i].name != port->get_port_name() || outputs[i].type != static_cast<Variant::Type>(port->get_port_type())) {
			add_warning(String("GLSL output {0} is `{1}: {2}`, expected `{3}: {4}`.")
								.format(varray(i,
										outputs[i].name,
										type_to_string(outputs[i].type),
										port->get_port_name(),
										type_to_string(static_cast<Variant::Type>(port->get_port_type())))));
		}
	}
}

bool VoxelGraphScriptNode::validate() {
	return validate(false);
}

bool VoxelGraphScriptNode::validate(bool refresh_metadata_from_script) {
	if (_validated_revision == _revision) {
		return _valid;
	}

	clear_validation();
	_validated_revision = _revision;
	if (get_script_path().is_empty()) {
		_valid = false;
		_gpu_compatible = false;
		return false;
	}
	validate_gdscript(refresh_metadata_from_script);
	if (_errors.size() > 0) {
		return false;
	}
	validate_metadata();
	if (_errors.size() > 0) {
		return false;
	}
	const int warning_count_before_glsl = _warnings.size();
	validate_glsl();
	_valid = _errors.size() == 0;
	_gpu_compatible = _valid && _warnings.size() == warning_count_before_glsl;
	return _valid;
}

#ifdef TOOLS_ENABLED
void VoxelGraphScriptNode::get_configuration_warnings(PackedStringArray &out_warnings) const {
	for (int i = 0; i < _errors.size(); ++i) {
		out_warnings.append(_errors[i]);
	}
	for (int i = 0; i < _warnings.size(); ++i) {
		out_warnings.append(_warnings[i]);
	}
}
#endif

void VoxelGraphScriptNode::_bind_methods() {
	using Self = VoxelGraphScriptNode;

	ClassDB::bind_method(D_METHOD("set_script_path", "path"), &Self::set_script_path);
	ClassDB::bind_method(D_METHOD("get_script_path"), &Self::get_script_path);
	ClassDB::bind_method(D_METHOD("set_attached_script", "script"), &Self::set_attached_script);
	ClassDB::bind_method(D_METHOD("get_attached_script"), &Self::get_attached_script);
	ClassDB::bind_method(D_METHOD("reload_attached_script"), &Self::reload_attached_script);
	ClassDB::bind_method(D_METHOD("reload_script_contract", "path"), &Self::reload_script_contract);
	ClassDB::bind_method(D_METHOD("refresh_metadata"), &Self::refresh_metadata);
	ClassDB::bind_method(D_METHOD("set_shader_path", "path"), &Self::set_shader_path);
	ClassDB::bind_method(D_METHOD("get_shader_path"), &Self::get_shader_path);
	ClassDB::bind_method(D_METHOD("set_entry_point", "entry_point"), &Self::set_entry_point);
	ClassDB::bind_method(D_METHOD("get_entry_point"), &Self::get_entry_point);
	ClassDB::bind_method(D_METHOD("set_inputs", "input_ports"), &Self::set_inputs);
	ClassDB::bind_method(D_METHOD("get_inputs"), &Self::get_inputs);
	ClassDB::bind_method(D_METHOD("clear_inputs"), &Self::clear_inputs);
	ClassDB::bind_method(D_METHOD("add_input", "name", "type"), &Self::add_input, DEFVAL(Variant::FLOAT));
	ClassDB::bind_method(D_METHOD("set_outputs", "output_ports"), &Self::set_outputs);
	ClassDB::bind_method(D_METHOD("get_outputs"), &Self::get_outputs);
	ClassDB::bind_method(D_METHOD("clear_outputs"), &Self::clear_outputs);
	ClassDB::bind_method(D_METHOD("add_output", "name", "type"), &Self::add_output, DEFVAL(Variant::FLOAT));
	ClassDB::bind_method(D_METHOD("set_parameters", "parameters"), &Self::set_parameters);
	ClassDB::bind_method(D_METHOD("get_parameters"), &Self::get_parameters);
	ClassDB::bind_method(D_METHOD("set_parameter_value", "name", "value"), &Self::set_parameter_value);
	ClassDB::bind_method(D_METHOD("get_parameter_value", "name"), &Self::get_parameter_value);
	ClassDB::bind_method(D_METHOD("validate"), static_cast<bool (Self::*)()>(&Self::validate));
	ClassDB::bind_method(D_METHOD("get_validation_errors"), &Self::get_validation_errors);
	ClassDB::bind_method(D_METHOD("get_validation_warnings"), &Self::get_validation_warnings);
	ClassDB::bind_method(D_METHOD("is_valid"), &Self::is_valid);
	ClassDB::bind_method(D_METHOD("is_gpu_compatible"), &Self::is_gpu_compatible);

	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, SGN_SCRIPT_PROPERTY_NAME, PROPERTY_HINT_RESOURCE_TYPE, "Script", PROPERTY_USAGE_EDITOR),
			"set_attached_script",
			"get_attached_script"
	);
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "script_path", PROPERTY_HINT_FILE, "*.gd"), "set_script_path", "get_script_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "shader_path", PROPERTY_HINT_FILE, "*.glsl"), "set_shader_path", "get_shader_path");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "entry_point"), "set_entry_point", "get_entry_point");
	ADD_PROPERTY(
			PropertyInfo(
					Variant::ARRAY,
					"input_ports",
					PROPERTY_HINT_ARRAY_TYPE,
					MAKE_RESOURCE_TYPE_HINT(VoxelGraphScriptNodePort::get_class_static())
			),
			"set_inputs",
			"get_inputs"
	);
	ADD_PROPERTY(
			PropertyInfo(
					Variant::ARRAY,
					"output_ports",
					PROPERTY_HINT_ARRAY_TYPE,
					MAKE_RESOURCE_TYPE_HINT(VoxelGraphScriptNodePort::get_class_static())
			),
			"set_outputs",
			"get_outputs"
	);
	ADD_PROPERTY(
			PropertyInfo(
					Variant::ARRAY,
					"parameters",
					PROPERTY_HINT_ARRAY_TYPE,
					MAKE_RESOURCE_TYPE_HINT(VoxelGraphScriptNodeParameter::get_class_static())
			),
			"set_parameters",
			"get_parameters"
	);
}

} // namespace zylann::voxel::pg
