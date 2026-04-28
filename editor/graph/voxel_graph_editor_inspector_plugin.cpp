#include "voxel_graph_editor_inspector_plugin.h"
#include "editor_property_text_change_on_submit.h"
#include "rune_noise_viewer.h"
#include "voxel_graph_node_inspector_wrapper.h"

namespace zylann::voxel {

bool VoxelGraphEditorInspectorPlugin::_zn_can_handle(const Object *obj) const {
	return obj != nullptr && Object::cast_to<VoxelGraphNodeInspectorWrapper>(obj) != nullptr;
}

void VoxelGraphEditorInspectorPlugin::_zn_parse_begin(Object *p_object) {
	VoxelGraphNodeInspectorWrapper *wrapper = Object::cast_to<VoxelGraphNodeInspectorWrapper>(p_object);
	ERR_FAIL_COND(wrapper == nullptr);

	Ref<pg::VoxelGraphFunction> graph = wrapper->get_graph();
	if (graph.is_null()) {
		return;
	}

	const uint32_t node_id = wrapper->get_node_id();
	if (!graph->has_node(node_id)) {
		return;
	}

	if (graph->get_node_type_id(node_id) == pg::VoxelGraphFunction::NODE_RUNE_NOISE) {
		RuneNoiseViewer *viewer = memnew(RuneNoiseViewer);
		viewer->set_node(graph, node_id);
		add_custom_control(viewer);
	}
}

bool VoxelGraphEditorInspectorPlugin::_zn_parse_property(Object *p_object, const Variant::Type p_type,
		const String &p_path, const PropertyHint p_hint, const String &p_hint_text,
		const BitField<PropertyUsageFlags> p_usage, const bool p_wide) {
	if (p_type == Variant::STRING && p_hint != PROPERTY_HINT_MULTILINE_TEXT) {
		add_property_editor(p_path, memnew(ZN_EditorPropertyTextChangeOnSubmit));
		return true;
	}
	return false;
}

} // namespace zylann::voxel
