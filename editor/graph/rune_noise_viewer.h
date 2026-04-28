#ifndef VOXEL_GRAPH_RUNE_NOISE_VIEWER_H
#define VOXEL_GRAPH_RUNE_NOISE_VIEWER_H

#include "../../generators/graph/voxel_graph_function.h"
#include "../../util/godot/classes/control.h"
#include "../../util/godot/macros.h"

ZN_GODOT_FORWARD_DECLARE(class TextureRect)

namespace zylann::voxel {

class RuneNoiseViewer : public Control {
	GDCLASS(RuneNoiseViewer, Control)
public:
	static const int PREVIEW_WIDTH = 300;
	static const int PREVIEW_HEIGHT = 150;

	RuneNoiseViewer();
	~RuneNoiseViewer();

	void set_node(Ref<pg::VoxelGraphFunction> graph, uint32_t node_id);

private:
	void _on_graph_changed();
	void _notification(int p_what);

	void disconnect_graph();
	void update_preview();

	static void _bind_methods() {}

	Ref<pg::VoxelGraphFunction> _graph;
	uint32_t _node_id = ProgramGraph::NULL_ID;
	float _time_before_update = -1.f;
	TextureRect *_texture_rect = nullptr;
};

} // namespace zylann::voxel

#endif // VOXEL_GRAPH_RUNE_NOISE_VIEWER_H
