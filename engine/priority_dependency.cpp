#include "priority_dependency.h"
#include "../constants/voxel_constants.h"
#include "../util/math/funcs.h"

namespace zylann::voxel {

uint8_t PriorityDependency::get_lod_priority_band(uint8_t lod_index) {
	return TaskPriority::BAND_MAX - math::min(lod_index, TaskPriority::BAND_MAX);
}

TaskPriority PriorityDependency::evaluate(uint8_t lod_index, uint8_t band2_priority, float *out_closest_distance_sq) {
	TaskPriority priority;
	ZN_ASSERT_RETURN_V(shared != nullptr, priority);

	const StdVector<Vector3f> &viewer_positions = shared->viewers;
	const unsigned int viewer_count = shared->viewers_count;

	const Vector3f block_position = world_position;

	float closest_distance_sq = 99999.f;
	if (viewer_positions.size() == 0) {
		// Assume origin
		closest_distance_sq = math::length_squared(block_position);
	} else {
		for (unsigned int i = 0; i < viewer_count; ++i) {
			const float d = math::distance_squared(viewer_positions[i], block_position);
			if (d < closest_distance_sq) {
				closest_distance_sq = d;
			}
		}
	}

	if (out_closest_distance_sq != nullptr) {
		*out_closest_distance_sq = closest_distance_sq;
	}

	// TODO Any way to optimize out the sqrt? Maybe with a fast integer version?
	// I added it because the LOD modifier was not working with squared distances,
	// which led blocks to subdivide too much compared to their neighbors, making cracks more likely to happen
	const int distance = static_cast<int>(Math::sqrt(closest_distance_sq));

	// Closer is higher priority. Decreases over distance.
	priority.band0 = math::max(TaskPriority::BAND_MAX - math::arithmetic_rshift(distance, 4 + lod_index), 0);
	// LOD takes precedence over distance, so mesh generation proceeds from LOD 0, then LOD 1, and so on.
	priority.band1 = get_lod_priority_band(lod_index);
	priority.band2 = band2_priority;
	priority.band3 = constants::TASK_PRIORITY_BAND3_DEFAULT;

	return priority;
}

} // namespace zylann::voxel
