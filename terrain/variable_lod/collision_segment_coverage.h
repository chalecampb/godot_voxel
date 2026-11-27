#ifndef VOXEL_COLLISION_SEGMENT_COVERAGE_H
#define VOXEL_COLLISION_SEGMENT_COVERAGE_H

#include "../../util/godot/core/vector3.h"

#include <cmath>
#include <cstdint>

namespace zylann::voxel {

// Pure helpers shared by VoxelLodTerrain's nonblocking collision-coverage query. The query returns a plain status
// value, so hot gameplay reads allocate nothing.
struct CollisionSegmentCoverage {
	enum Status : uint8_t {
		COLLISION_SEGMENT_READY,
		COLLISION_SEGMENT_UNAVAILABLE,
		COLLISION_SEGMENT_OVER_BUDGET,
		COLLISION_SEGMENT_INVALID,
		COLLISION_SEGMENT_UNSUPPORTED
	};

	// Largest accepted leaf budget. Larger positive requests are capped to it.
	static constexpr int MAX_LEAVES = 32;

	// Non-positive budgets are invalid; the endpoints must be finite.
	static bool is_valid_query(const Vector3 &from, const Vector3 &to, int max_leaves) {
		return is_finite(from) && is_finite(to) && max_leaves > 0;
	}

	static unsigned int clamp_max_leaves(int max_leaves) {
		return max_leaves > MAX_LEAVES ? unsigned(MAX_LEAVES) : unsigned(max_leaves);
	}

	// Closed slab test: touching a face, edge, or corner counts as an intersection.
	static bool segment_intersects_closed_box(
			const Vector3 &from,
			const Vector3 &to,
			const Vector3 &box_min,
			const Vector3 &box_max
	) {
		double t_min = 0.0;
		double t_max = 1.0;
		for (unsigned int axis = 0; axis < 3; ++axis) {
			const double origin = from[axis];
			const double delta = double(to[axis]) - origin;
			const double lower = box_min[axis];
			const double upper = box_max[axis];
			if (delta == 0.0) {
				if (origin < lower || origin > upper) {
					return false;
				}
				continue;
			}
			double t1 = (lower - origin) / delta;
			double t2 = (upper - origin) / delta;
			if (t1 > t2) {
				const double tmp = t1;
				t1 = t2;
				t2 = tmp;
			}
			t_min = t1 > t_min ? t1 : t_min;
			t_max = t2 < t_max ? t2 : t_max;
			if (t_min > t_max) {
				return false;
			}
		}
		return true;
	}

private:
	static bool is_finite(const Vector3 &v) {
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}
};

} // namespace zylann::voxel

#endif // VOXEL_COLLISION_SEGMENT_COVERAGE_H
