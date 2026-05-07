#include "voxel_graph_shader_generator.h"
#include "../../engine/gpu/compute_shader_parameters.h"
#include "../../engine/gpu/compute_shader_resource.h"
#include "../../util/containers/container_funcs.h"
#include "../../util/containers/std_unordered_map.h"
#include "../../util/godot/classes/file_access.h"
#include "../../util/godot/core/array.h" // for `varray` in GDExtension builds
#include "../../util/godot/core/string.h"
#include "../../util/profiling.h"
#include "../../util/string/format.h"
#include "node_type_db.h"
#include "voxel_graph_compiler.h"
#include "voxel_graph_script_node.h"
#include <regex>
#include <sstream>

namespace zylann::voxel::pg {

void ShaderGenContext::require_lib_code(const char *lib_name, const char *code) {
	_code_gen.require_lib_code(lib_name, code);
}

void ShaderGenContext::require_lib_code(const char *lib_name, const char **code) {
	_code_gen.require_lib_code(lib_name, code);
}

StdString ShaderGenContext::add_uniform(std::shared_ptr<ComputeShaderResource> res) {
	StdString name = format("u_vg_resource_{}", _uniforms.size());
	_uniforms.push_back(ShaderParameter());
	ShaderParameter &sp = _uniforms.back();
	sp.name = name;
	sp.resource = std::move(res);
	return name;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace {

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

StdString get_glsl_type_name(Variant::Type type) {
	switch (type) {
		case Variant::FLOAT:
			return "float";
		case Variant::INT:
			return "int";
		case Variant::BOOL:
			return "bool";
		default:
			return "float";
	}
}

StdString get_glsl_literal(Variant value, Variant::Type type) {
	switch (type) {
		case Variant::FLOAT:
			return format("{}", value.operator float());
		case Variant::INT:
			return format("{}", value.operator int());
		case Variant::BOOL:
			return value.operator bool() ? "true" : "false";
		default:
			return "0.0";
	}
}

bool is_glsl_identifier_char(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

std::string sanitize_glsl_identifier(std::string s) {
	for (char &c : s) {
		if (!is_glsl_identifier_char(c)) {
			c = '_';
		}
	}
	if (s.empty() || (s[0] >= '0' && s[0] <= '9')) {
		s.insert(s.begin(), '_');
	}
	return s;
}

void replace_glsl_identifier(std::string &source, const std::string &from, const std::string &to) {
	if (from == to || from.empty()) {
		return;
	}

	size_t pos = 0;
	while ((pos = source.find(from, pos)) != std::string::npos) {
		const bool left_boundary = pos == 0 || !is_glsl_identifier_char(source[pos - 1]);
		const size_t end = pos + from.size();
		const bool right_boundary = end == source.size() || !is_glsl_identifier_char(source[end]);
		if (left_boundary && right_boundary) {
			source.replace(pos, from.size(), to);
			pos += to.size();
		} else {
			pos = end;
		}
	}
}

StdVector<std::string> collect_script_graph_node_global_identifiers(const std::string &source) {
	StdVector<std::string> identifiers;

	const std::regex uniform_regex("\\buniform\\s+(?:float|int|bool)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*;");
	for (std::sregex_iterator it(source.begin(), source.end(), uniform_regex); it != std::sregex_iterator(); ++it) {
		identifiers.push_back((*it)[1].str());
	}

	const std::regex function_regex(
			"\\b(?:void|float|int|bool|vec2|vec3|vec4|ivec2|ivec3|ivec4|bvec2|bvec3|bvec4)\\s+"
			"([A-Za-z_][A-Za-z0-9_]*)\\s*\\([^;{}]*\\)\\s*\\{"
	);
	for (std::sregex_iterator it(source.begin(), source.end(), function_regex); it != std::sregex_iterator(); ++it) {
		identifiers.push_back((*it)[1].str());
	}

	return identifiers;
}

std::string get_script_graph_node_shader_prefix(Ref<VoxelGraphScriptNode> script_node, uint32_t node_id) {
	const String script_namespace_gd = script_node->get_script_path().get_file().get_basename();
	const StdString script_namespace_tmp = zylann::godot::to_std_string(script_namespace_gd);
	return format(
			"{}_{}_",
			sanitize_glsl_identifier(std::string(script_namespace_tmp.c_str())).c_str(),
			node_id
	).c_str();
}

CompilationResult make_shader_error(String message) {
	CompilationResult result;
	result.success = false;
	result.message = message;
	return result;
}

CompilationResult get_script_graph_node_shader_source(
		Ref<VoxelGraphScriptNode> script_node,
		uint32_t node_id,
		StdString &out_function_name,
		StdString &out_source_code
) {
	if (script_node.is_null()) {
		return CompilationResult::make_error("ScriptGraphNode has no VoxelGraphScriptNode resource assigned.");
	}

	if (!script_node->validate()) {
		PackedStringArray errors = script_node->get_validation_errors();
		const String message = errors.size() > 0 ? errors[0] : String("ScriptGraphNode contract is invalid.");
		return make_shader_error(message);
	}

	if (!script_node->is_gpu_compatible()) {
		PackedStringArray warnings = script_node->get_validation_warnings();
		const String message =
				warnings.size() > 0 ? warnings[0] : String("ScriptGraphNode has no GPU-compatible GLSL backing.");
		return make_shader_error(message);
	}

	String source_text;
	String read_error;
	if (!read_text_file(script_node->get_shader_path(), source_text, read_error)) {
		return make_shader_error(String("ScriptGraphNode GLSL file error: {0}").format(varray(read_error)));
	}

	const StdString source_tmp = zylann::godot::to_std_string(source_text);
	std::string source(source_tmp.c_str());

	const std::string node_prefix = get_script_graph_node_shader_prefix(script_node, node_id);
	const StdVector<std::string> global_identifiers = collect_script_graph_node_global_identifiers(source);
	for (const std::string &identifier : global_identifiers) {
		replace_glsl_identifier(source, identifier, node_prefix + identifier);
	}

	for (const Ref<VoxelGraphScriptNodeParameter> &parameter : script_node->get_parameter_definitions()) {
		if (parameter.is_null()) {
			continue;
		}

		const String parameter_name = parameter->get_parameter_name();
		const StdString parameter_name_tmp = zylann::godot::to_std_string(parameter_name);
		const std::string parameter_name_std(parameter_name_tmp.c_str());
		const Variant::Type parameter_type = static_cast<Variant::Type>(parameter->get_parameter_type());
		const std::string namespaced_parameter_name = node_prefix + parameter_name_std;

		const std::regex uniform_regex("\\buniform\\s+(float|int|bool)\\s+" + namespaced_parameter_name + "\\s*;");
		const StdString glsl_type = get_glsl_type_name(parameter_type);
		const StdString literal = get_glsl_literal(script_node->get_parameter_value(parameter_name), parameter_type);
		const std::string replacement = "const " + std::string(glsl_type.c_str()) + " " + namespaced_parameter_name +
				" = " + literal.c_str() + ";";
		source = std::regex_replace(source, uniform_regex, replacement);
	}

	const StdString entry_point_tmp = zylann::godot::to_std_string(script_node->get_entry_point());
	const std::string entry_point(entry_point_tmp.c_str());
	out_function_name = (node_prefix + entry_point).c_str();

	const std::regex function_regex("\\bvoid\\s+" + std::string(out_function_name.c_str()) + "\\s*\\(");
	if (!std::regex_search(source, function_regex)) {
		return make_shader_error(
				String("ScriptGraphNode GLSL entry point `{0}` was not found.")
						.format(varray(script_node->get_entry_point()))
		);
	}

	out_source_code = source.c_str();
	return CompilationResult::make_success();
}

} // namespace

CompilationResult generate_shader(
		const ProgramGraph &p_graph,
		Span<const VoxelGraphFunction::Port> input_defs,
		FwdMutableStdString source_code,
		StdVector<ShaderParameter> &shader_params,
		StdVector<ShaderOutput> &outputs,
		Span<const VoxelGraphFunction::NodeTypeID> restricted_outputs
) {
	ZN_PROFILE_SCOPE();

	const NodeTypeDB &type_db = NodeTypeDB::get_singleton();

	ProgramGraph expanded_graph;
	const CompilationResult expand_result =
			expand_graph(p_graph, expanded_graph, input_defs, nullptr, type_db, nullptr, true);
	if (!expand_result.success) {
		return expand_result;
	}

	StdVector<uint32_t> order;
	StdVector<uint32_t> terminal_nodes;

	expanded_graph.for_each_node_const([&terminal_nodes, restricted_outputs, &type_db](const ProgramGraph::Node &node) {
		const NodeType &node_type = type_db.get_type(node.type_id);
		if (node_type.category == pg::CATEGORY_OUTPUT) {
			if (restricted_outputs.size() == 0) {
				// Get all outputs
				terminal_nodes.push_back(node.id);
			} else {
				// Only get dependencies of specific outputs
				if (contains(restricted_outputs, VoxelGraphFunction::NodeTypeID(node.type_id))) {
					terminal_nodes.push_back(node.id);
				}
			}
		}
	});

	if (terminal_nodes.size() == 0) {
		return CompilationResult::make_error("Can't generate shader, the graph does not contain the required outputs.");
	}

	// Exclude debug nodes
	// unordered_remove_if(terminal_nodes, [&expanded_graph, &type_db](uint32_t node_id) {
	// 	const ProgramGraph::Node &node = expanded_graph.get_node(node_id);
	// 	const NodeType &type = type_db.get_type(node.type_id);
	// 	return type.debug_only;
	// });

	expanded_graph.find_dependencies(to_span(terminal_nodes), order);

	CodeGenHelper codegen;

	codegen.add("void generate(vec3 pos");

	for (const uint32_t node_id : order) {
		const ProgramGraph::Node &node = expanded_graph.get_node(node_id);
		const NodeType &node_type = type_db.get_type(node.type_id);

		if (node_type.category == CATEGORY_OUTPUT) {
			switch (node.type_id) {
				case VoxelGraphFunction::NODE_OUTPUT_SDF:
					codegen.add(", out float out_sd");
					outputs.push_back(ShaderOutput{ ShaderOutput::TYPE_SDF });
					break;
				case VoxelGraphFunction::NODE_OUTPUT_SINGLE_TEXTURE:
					codegen.add(", out float out_single_texture");
					outputs.push_back(ShaderOutput{ ShaderOutput::TYPE_SINGLE_TEXTURE });
					break;
				case VoxelGraphFunction::NODE_OUTPUT_TYPE:
					codegen.add(", out float out_type");
					outputs.push_back(ShaderOutput{ ShaderOutput::TYPE_TYPE });
					break;
				default:
					ZN_PRINT_WARNING(
							format("Output type {} is not supported yet in shader generator.", node_type.name)
					);
					break;
			}
		}
	}

	codegen.add(") {\n");

	codegen.indent();

	// This map only contains output ports.
	StdUnorderedMap<ProgramGraph::PortLocation, StdString> port_to_var;

	FixedArray<StdString, 8> unconnected_input_var_names;

	FixedArray<const char *, 8> input_names;
	FixedArray<const char *, 8> output_names;

	for (const uint32_t node_id : order) {
		const ProgramGraph::Node &node = expanded_graph.get_node(node_id);
		const NodeType node_type = type_db.get_type(node.type_id);

		switch (node.type_id) {
			case VoxelGraphFunction::NODE_INPUT_X: {
				ZN_ASSERT(node.outputs.size() == 1);
				const ProgramGraph::PortLocation output_port{ node_id, 0 };
				port_to_var.insert({ output_port, "pos.x" });
				continue;
			}
			case VoxelGraphFunction::NODE_INPUT_Y: {
				ZN_ASSERT(node.outputs.size() == 1);
				const ProgramGraph::PortLocation output_port{ node_id, 0 };
				port_to_var.insert({ output_port, "pos.y" });
				continue;
			}
			case VoxelGraphFunction::NODE_INPUT_Z: {
				ZN_ASSERT(node.outputs.size() == 1);
				const ProgramGraph::PortLocation output_port{ node_id, 0 };
				port_to_var.insert({ output_port, "pos.z" });
				continue;
			}
			case VoxelGraphFunction::NODE_CONSTANT: {
				ZN_ASSERT(node.outputs.size() == 1);
				const ProgramGraph::PortLocation output_port{ node_id, 0 };
				const StdString name = codegen.generate_var_name();
				port_to_var.insert({ output_port, name });
				ZN_ASSERT(node.params.size() == 1);
				codegen.add_format("float {} = {};\n", name, float(node.params[0]));
				continue;
			}
			case VoxelGraphFunction::NODE_OUTPUT_SDF: {
				ZN_ASSERT(node.outputs.size() == 1);
				const ProgramGraph::Port &input_port = node.inputs[0];
				if (input_port.connections.size() > 0) {
					ZN_ASSERT(input_port.connections.size() == 1);
					auto it = port_to_var.find(input_port.connections[0]);
					ZN_ASSERT(it != port_to_var.end());
					codegen.add_format("out_sd = {};\n", it->second);
				} else {
					codegen.add_format("out_sd = {};\n", float(node.default_inputs[0]));
				}
				continue;
			}
			case VoxelGraphFunction::NODE_OUTPUT_SINGLE_TEXTURE: {
				ZN_ASSERT(node.outputs.size() == 1);
				const ProgramGraph::Port &input_port = node.inputs[0];
				if (input_port.connections.size() > 0) {
					ZN_ASSERT(input_port.connections.size() == 1);
					auto it = port_to_var.find(input_port.connections[0]);
					ZN_ASSERT(it != port_to_var.end());
					codegen.add_format("out_single_texture = {};\n", it->second);
				} else {
					codegen.add_format("out_single_texture = {};\n", float(node.default_inputs[0]));
				}
				continue;
			}
			case VoxelGraphFunction::NODE_OUTPUT_TYPE: {
				ZN_ASSERT(node.outputs.size() == 1);
				const ProgramGraph::Port &input_port = node.inputs[0];
				if (input_port.connections.size() > 0) {
					ZN_ASSERT(input_port.connections.size() == 1);
					auto it = port_to_var.find(input_port.connections[0]);
					ZN_ASSERT(it != port_to_var.end());
					codegen.add_format("out_type = {};\n", it->second);
				} else {
					codegen.add_format("out_type = {};\n", float(node.default_inputs[0]));
				}
				continue;
			}
			// TODO Custom inputs
			// TODO Custom outputs
			default:
				break;
		}

		const bool is_script_graph_node = node.type_id == VoxelGraphFunction::NODE_SCRIPT_GRAPH;

		if (node_type.shader_gen_func == nullptr && !is_script_graph_node) {
			CompilationResult error;
			error.message =
					String("The node {0} does not support conversion to shader.").format(varray(node_type.name));
			error.node_id = node_id;
			return error;
		}

		for (unsigned int port_index = 0; port_index < node.inputs.size(); ++port_index) {
			const ProgramGraph::Port &input_port = node.inputs[port_index];
			if (input_port.connections.size() > 0) {
				ZN_ASSERT(input_port.connections.size() == 1);
				auto it = port_to_var.find(input_port.connections[0]);
				ZN_ASSERT(it != port_to_var.end());
				input_names[port_index] = it->second.c_str();
			} else {
				// No incoming connections to this input. Make up a variable so following code can stay the same.
				// It will only be used for this node.
				StdString &var_name = unconnected_input_var_names[port_index];
				var_name = codegen.generate_var_name();
				input_names[port_index] = var_name.c_str();
				codegen.add_format("float {} = {};\n", var_name, float(node.default_inputs[port_index]));
			}
		}

		for (unsigned int port_index = 0; port_index < node.outputs.size(); ++port_index) {
			const StdString var_name = codegen.generate_var_name();
			auto p = port_to_var.insert({ { node_id, port_index }, var_name });
			ZN_ASSERT(p.second); // Conflict with an existing port?
			output_names[port_index] = p.first->second.c_str();
			codegen.add_format("float {};\n", var_name.c_str());
		}

		codegen.add("{\n");
		codegen.indent();

		ShaderGenContext ctx(
				node.params,
				to_span(input_names, node.inputs.size()),
				to_span(output_names, node.outputs.size()),
				codegen,
				shader_params
		);
		if (is_script_graph_node) {
			Ref<VoxelGraphScriptNode> script_node = ctx.get_param(0);
			StdString function_name;
			StdString source_code;
			CompilationResult result = get_script_graph_node_shader_source(script_node, node_id, function_name, source_code);
			if (!result.success) {
				result.node_id = node_id;
				return result;
			}
			codegen.require_lib_code(function_name.c_str(), source_code.c_str());
			ctx.add_format("{}(", function_name);
			for (unsigned int port_index = 0; port_index < node.inputs.size(); ++port_index) {
				if (port_index > 0) {
					ctx.add_format("{}", ", ");
				}
				ctx.add_format("{}", ctx.get_input_name(port_index));
			}
			for (unsigned int port_index = 0; port_index < node.outputs.size(); ++port_index) {
				if (node.inputs.size() > 0 || port_index > 0) {
					ctx.add_format("{}", ", ");
				}
				ctx.add_format("{}", ctx.get_output_name(port_index));
			}
			ctx.add_format("{}", ");\n");
		} else {
			node_type.shader_gen_func(ctx);
		}

		if (ctx.has_error()) {
			CompilationResult result;
			result.success = false;
			result.message = ctx.get_error_message();
			result.node_id = node_id;
			return result;
		}

		codegen.dedent();
		codegen.add("}\n");
	}

	codegen.dedent();
	codegen.add("}\n");

	source_code.s = codegen.print();

	CompilationResult result;
	result.success = true;
	return result;
}

} // namespace zylann::voxel::pg
