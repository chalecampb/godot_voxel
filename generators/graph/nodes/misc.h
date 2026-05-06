#include "../node_type_db.h"
#include "../voxel_graph_script_node.h"
#include "../../../util/containers/std_unordered_map.h"
#include "../../../util/thread/mutex.h"
#include "../../../util/thread/thread.h"

namespace zylann::voxel::pg {

inline float select(float a, float b, float threshold, float t) {
	return t < threshold ? a : b;
}

void register_misc_nodes(Span<NodeType> types) {
	using namespace math;

	struct L {
		static Variant create_script_graph_node_resource() {
			Ref<VoxelGraphScriptNode> res;
			res.instantiate();
			return Variant(res);
		}
	};

	struct ScriptGraphNodeRuntimeData {
		Ref<VoxelGraphScriptNode> script_node;
		Mutex mutex;
		StdUnorderedMap<Thread::ID, Ref<VoxelGraphScriptNode>> thread_nodes;

		Ref<VoxelGraphScriptNode> get_thread_node() {
			const Thread::ID thread_id = Thread::get_caller_id();
			MutexLock lock(mutex);
			auto it = thread_nodes.find(thread_id);
			if (it != thread_nodes.end()) {
				return it->second;
			}

			Ref<Resource> duplicate = script_node->duplicate();
			Ref<VoxelGraphScriptNode> thread_node = duplicate;
			if (thread_node.is_null()) {
				thread_node = script_node;
			}
			thread_nodes.insert({ thread_id, thread_node });
			return thread_node;
		}
	};

	struct ScriptGraphNodeParams {
		ScriptGraphNodeRuntimeData *runtime_data;
		uint32_t input_count;
		uint32_t output_count;
	};

	{
		NodeType &t = types[VoxelGraphFunction::NODE_CONSTANT];
		t.name = "Constant";
		t.category = CATEGORY_CONSTANT;
		t.outputs.push_back(NodeType::Port("value"));
		t.params.push_back(NodeType::Param("value", Variant::FLOAT));
	}
	{
		NodeType &t = types[VoxelGraphFunction::NODE_RELAY];
		t.name = "Relay";
		t.category = CATEGORY_RELAY;
		t.inputs.push_back(NodeType::Port("in"));
		t.outputs.push_back(NodeType::Port("out"));
		t.is_pseudo_node = true;
	}
	{
		NodeType &t = types[VoxelGraphFunction::NODE_COMMENT];
		t.name = "Comment";
		t.category = CATEGORY_DEBUG;
		NodeType::Param text_param("text", Variant::STRING, Variant(""));
		text_param.multiline = true;
		t.params.push_back(text_param);
		t.debug_only = true;
		t.is_pseudo_node = true;
	}
	{
		NodeType &t = types[VoxelGraphFunction::NODE_FUNCTION];
		t.name = "Function";
		t.category = CATEGORY_FUNCTIONS;

		NodeType::Param func_param("_function", VoxelGraphFunction::get_class_static(), nullptr);
		func_param.hidden = true;
		t.params.push_back(func_param);

		t.debug_only = false;
		t.is_pseudo_node = true;
	}
	{
		NodeType &t = types[VoxelGraphFunction::NODE_SCRIPT_GRAPH];
		t.name = "ScriptGraphNode";
		t.category = CATEGORY_GENERATE;

		NodeType::Param node_param(
				"script_node", VoxelGraphScriptNode::get_class_static(), &L::create_script_graph_node_resource
		);
		node_param.hidden = true;
		t.params.push_back(node_param);

		t.compile_func = [](CompileContext &ctx) {
			Ref<VoxelGraphScriptNode> script_node = ctx.get_param(0);
			if (script_node.is_null()) {
				ctx.make_error(ZN_TTR("ScriptGraphNode has no VoxelGraphScriptNode resource assigned."));
				return;
			}
			if (script_node->get_script_path().is_empty()) {
				ctx.make_error(ZN_TTR("ScriptGraphNode has no GDScript assigned."));
				return;
			}
			if (!script_node->validate()) {
				PackedStringArray errors = script_node->get_validation_errors();
				if (errors.size() > 0) {
					ctx.make_error(errors[0]);
				} else {
					ctx.make_error(ZN_TTR("ScriptGraphNode contract is invalid."));
				}
				return;
			}
			ScriptGraphNodeRuntimeData *runtime_data = ZN_NEW(ScriptGraphNodeRuntimeData);
			runtime_data->script_node = script_node;
			ctx.add_delete_cleanup(runtime_data);

			ctx.set_params(ScriptGraphNodeParams{ runtime_data,
					static_cast<uint32_t>(script_node->get_input_ports().size()),
					static_cast<uint32_t>(script_node->get_output_ports().size()) });
		};

		t.process_buffer_func = [](Runtime::ProcessBufferContext &ctx) {
			const ScriptGraphNodeParams &params = ctx.get_params<ScriptGraphNodeParams>();
			ZN_ASSERT_RETURN(params.runtime_data != nullptr);
			Ref<VoxelGraphScriptNode> script_node = params.runtime_data->get_thread_node();
			ZN_ASSERT_RETURN(script_node.is_valid());

			const Span<const Ref<VoxelGraphScriptNodePort>> input_ports = script_node->get_input_ports();
			const Span<const Ref<VoxelGraphScriptNodePort>> output_ports = script_node->get_output_ports();
			ZN_ASSERT_RETURN(input_ports.size() == params.input_count);
			ZN_ASSERT_RETURN(output_ports.size() == params.output_count);

			Dictionary inputs;
			Dictionary outputs;
			for (uint32_t output_index = 0; output_index < params.output_count; ++output_index) {
				const Ref<VoxelGraphScriptNodePort> &port = output_ports[output_index];
				if (port.is_valid()) {
					outputs[port->get_port_name()] = 0.f;
				}
			}

			const uint32_t buffer_size = params.output_count > 0 ? ctx.get_output(0).size : 0;
			for (uint32_t buffer_index = 0; buffer_index < buffer_size; ++buffer_index) {
				for (uint32_t input_index = 0; input_index < params.input_count; ++input_index) {
					const Ref<VoxelGraphScriptNodePort> &port = input_ports[input_index];
					if (port.is_valid()) {
						const Runtime::Buffer &input = ctx.get_input(input_index);
						inputs[port->get_port_name()] =
								input.is_constant ? input.constant_value : input.data[buffer_index];
					}
				}

				script_node->generate(inputs, outputs);

				for (uint32_t output_index = 0; output_index < params.output_count; ++output_index) {
					const Ref<VoxelGraphScriptNodePort> &port = output_ports[output_index];
					if (port.is_valid()) {
						Runtime::Buffer &output = ctx.get_output(output_index);
						output.data[buffer_index] = outputs.get(port->get_port_name(), 0.f).operator float();
					}
				}
			}
		};

		t.range_analysis_func = [](Runtime::RangeAnalysisContext &ctx) {
			const ScriptGraphNodeParams &params = ctx.get_params<ScriptGraphNodeParams>();
			for (uint32_t output_index = 0; output_index < params.output_count; ++output_index) {
				ctx.set_output(output_index, math::Interval::from_infinity());
			}
		};

		t.debug_only = false;
		t.is_pseudo_node = false;
	}
	{
		struct Params {
			float threshold;
		};
		NodeType &t = types[VoxelGraphFunction::NODE_SELECT];
		// t < threshold ? a : b
		t.name = "Select";
		t.category = CATEGORY_CONVERT;
		t.inputs.push_back(NodeType::Port("a"));
		t.inputs.push_back(NodeType::Port("b"));
		t.inputs.push_back(NodeType::Port("t"));
		t.outputs.push_back(NodeType::Port("out"));
		t.params.push_back(NodeType::Param("threshold", Variant::FLOAT, 0.f));

		t.compile_func = [](CompileContext &ctx) {
			Params p;
			p.threshold = ctx.get_param(0).operator float();
			ctx.set_params(p);
		};

		t.process_buffer_func = [](Runtime::ProcessBufferContext &ctx) {
			bool a_ignored;
			bool b_ignored;
			const Runtime::Buffer &a = ctx.try_get_input(0, a_ignored);
			const Runtime::Buffer &b = ctx.try_get_input(1, b_ignored);
			const Runtime::Buffer &tested_value = ctx.get_input(2);
			const float threshold = ctx.get_params<Params>().threshold;

			Runtime::Buffer &out = ctx.get_output(0);

			const uint32_t buffer_size = out.size;

			if (a_ignored) {
				memcpy(out.data, b.data, buffer_size * sizeof(float));

			} else if (b_ignored) {
				memcpy(out.data, a.data, buffer_size * sizeof(float));

			} else if (tested_value.is_constant) {
				const float *src = tested_value.constant_value < threshold ? a.data : b.data;
				memcpy(out.data, src, buffer_size * sizeof(float));

			} else if (a.is_constant && b.is_constant && a.constant_value == b.constant_value) {
				memcpy(out.data, a.data, buffer_size * sizeof(float));

			} else {
				for (uint32_t i = 0; i < buffer_size; ++i) {
					out.data[i] = select(a.data[i], b.data[i], threshold, tested_value.data[i]);
				}
			}
		};

		t.range_analysis_func = [](Runtime::RangeAnalysisContext &ctx) {
			const Interval a = ctx.get_input(0);
			const Interval b = ctx.get_input(1);
			const Interval tested_value = ctx.get_input(2);
			const float threshold = ctx.get_params<Params>().threshold;

			if (tested_value.min >= threshold) {
				ctx.set_output(0, b);
				// `a` won't be used
				ctx.ignore_input(0);

			} else if (tested_value.max < threshold) {
				ctx.set_output(0, a);
				// `b` won't be used
				ctx.ignore_input(1);

			} else {
				ctx.set_output(0, Interval::from_union(a, b));
			}
		};

		t.shader_gen_func = [](ShaderGenContext &ctx) {
			const float threshold = ctx.get_param(0);
			ctx.add_format(
					"{} = {} < {} ? {} : {};\n",
					ctx.get_output_name(0),
					ctx.get_input_name(2),
					threshold,
					ctx.get_input_name(0),
					ctx.get_input_name(1)
			);
		};
	}
	{
		NodeType &t = types[VoxelGraphFunction::NODE_EXPRESSION];
		t.name = "Expression";
		t.category = CATEGORY_MATH;
		NodeType::Param expression_param("expression", Variant::STRING, "0");
		expression_param.multiline = false;
		t.params.push_back(expression_param);
		t.outputs.push_back(NodeType::Port("out"));
		t.compile_func = [](CompileContext &ctx) {
			ctx.make_error(ZN_TTR("Internal error, expression wasn't expanded"));
		};
		t.is_pseudo_node = true;
	}
}

} // namespace zylann::voxel::pg
