#include "rune_noise_viewer.h"
#include "../../constants/voxel_string_names.h"
#include "../../util/containers/std_vector.h"
#include "../../util/godot/classes/image.h"
#include "../../util/godot/classes/image_texture.h"
#include "../../util/godot/classes/texture_rect.h"
#include "../../util/godot/editor_scale.h"
#include "../../util/noise/rune_noise.h"

#ifdef ZN_GODOT
#include "../../util/godot/core/callable_mp.h"
#endif

namespace zylann::voxel {

RuneNoiseViewer::RuneNoiseViewer() {
	set_custom_minimum_size(Vector2(0, EDSCALE * PREVIEW_HEIGHT));

	_texture_rect = memnew(TextureRect);
	_texture_rect->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	_texture_rect->set_stretch_mode(TextureRect::STRETCH_KEEP_ASPECT_COVERED);
	add_child(_texture_rect);
}

RuneNoiseViewer::~RuneNoiseViewer() {
	disconnect_graph();
}

void RuneNoiseViewer::set_node(Ref<pg::VoxelGraphFunction> graph, uint32_t node_id) {
	if (_graph == graph && _node_id == node_id) {
		return;
	}

	disconnect_graph();

	_graph = graph;
	_node_id = node_id;

	if (_graph.is_valid()) {
		_graph->connect(VoxelStringNames::get_singleton().changed, callable_mp(this, &RuneNoiseViewer::_on_graph_changed));
		set_process(true);
		update_preview();

	} else {
		set_process(false);
		_time_before_update = -1.f;
	}
}

void RuneNoiseViewer::disconnect_graph() {
	if (_graph.is_valid()) {
		const Callable callable = callable_mp(this, &RuneNoiseViewer::_on_graph_changed);
		if (_graph->is_connected(VoxelStringNames::get_singleton().changed, callable)) {
			_graph->disconnect(VoxelStringNames::get_singleton().changed, callable);
		}
	}
}

void RuneNoiseViewer::_on_graph_changed() {
	_time_before_update = 0.25f;
}

void RuneNoiseViewer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PROCESS: {
			if (_time_before_update > 0.f) {
				_time_before_update -= get_process_delta_time();
				if (_time_before_update <= 0.f) {
					update_preview();
				}
			}
		} break;
	}
}

namespace {

RuneNoiseParams get_rune_noise_params(const pg::VoxelGraphFunction &graph, const uint32_t node_id) {
	RuneNoiseParams params;
	params.seed = graph.get_node_param(node_id, 0).operator int();
	params.erosion_scale = graph.get_node_param(node_id, 1);
	params.erosion_strength = graph.get_node_param(node_id, 2);
	params.erosion_slope_power = graph.get_node_param(node_id, 3);
	params.erosion_cell_scale = graph.get_node_param(node_id, 4);
	params.erosion_height_offset = graph.get_node_param(node_id, 5);
	params.erosion_octaves = graph.get_node_param(node_id, 6).operator int();
	params.erosion_gain = graph.get_node_param(node_id, 7);
	params.erosion_lacunarity = graph.get_node_param(node_id, 8);
	params.height_tiles = graph.get_node_param(node_id, 9);
	params.height_octaves = graph.get_node_param(node_id, 10).operator int();
	params.height_amp = graph.get_node_param(node_id, 11);
	params.height_gain = graph.get_node_param(node_id, 12);
	params.height_lacunarity = graph.get_node_param(node_id, 13);
	params.water_height = graph.get_node_param(node_id, 14);
	return params;
}

} // namespace

void RuneNoiseViewer::update_preview() {
	const Vector2i preview_size(PREVIEW_WIDTH, PREVIEW_HEIGHT);
	Ref<Image> im = godot::create_empty_image(preview_size.x, preview_size.y, false, Image::FORMAT_RGB8);

	if (_graph.is_valid() && _graph->has_node(_node_id) &&
		_graph->get_node_type_id(_node_id) == pg::VoxelGraphFunction::NODE_RUNE_NOISE) {
		const RuneNoiseParams params = get_rune_noise_params(**_graph, _node_id);
		StdVector<float> heights;
		heights.resize(preview_size.x * preview_size.y);

		float min_height = 0.f;
		float max_height = 0.f;
		bool first = true;
		unsigned int i = 0;
		for (int y = 0; y < preview_size.y; ++y) {
			for (int x = 0; x < preview_size.x; ++x) {
				const Vector2f p(
						static_cast<float>(x) / static_cast<float>(preview_size.x - 1),
						static_cast<float>(y) / static_cast<float>(preview_size.y - 1)
				);
				const float h = get_rune_noise_2d(p, params).height;
				heights[i] = h;
				if (first) {
					min_height = h;
					max_height = h;
					first = false;
				} else {
					min_height = math::min(min_height, h);
					max_height = math::max(max_height, h);
				}
				++i;
			}
		}

		const float inv_range = max_height > min_height ? 1.f / (max_height - min_height) : 1.f;
		i = 0;
		for (int y = 0; y < preview_size.y; ++y) {
			for (int x = 0; x < preview_size.x; ++x) {
				const float g = math::clamp((heights[i] - min_height) * inv_range, 0.f, 1.f);
				im->set_pixel(x, y, Color(g, g, g));
				++i;
			}
		}
	}

	Ref<ImageTexture> tex = ImageTexture::create_from_image(im);
	_texture_rect->set_texture(tex);
}

} // namespace zylann::voxel
