#ifndef VOXEL_COLLISION_SEGMENT_COVERAGE_H
#define VOXEL_COLLISION_SEGMENT_COVERAGE_H

#include "../../util/godot/core/vector3.h"
#include "../../util/godot/classes/ref_counted.h"
#include "../../util/godot/core/class_db.h"

#include <cmath>
#include <cstdint>

namespace zylann::voxel {

// Geometry and query-bound helpers shared by VoxelLodTerrain's nonblocking coverage query.
class CollisionSegmentCoverage : public RefCounted {
	GDCLASS(CollisionSegmentCoverage, RefCounted)
public:
	enum Status : uint8_t {
		COLLISION_SEGMENT_READY,
		COLLISION_SEGMENT_UNAVAILABLE,
		COLLISION_SEGMENT_OVER_BUDGET,
		COLLISION_SEGMENT_INVALID,
		COLLISION_SEGMENT_UNSUPPORTED
	};

	Status status = COLLISION_SEGMENT_UNAVAILABLE;
	unsigned int leaf_count = 0;

	Status get_status() const {
		return status;
	}

	unsigned int get_leaf_count() const {
		return leaf_count;
	}

	static void _bind_methods();

	static bool is_valid_query(const Vector3 &from, const Vector3 &to, unsigned int max_leaves) {
		return is_finite(from) && is_finite(to) && max_leaves > 0 && max_leaves <= 32;
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

VARIANT_ENUM_CAST(zylann::voxel::CollisionSegmentCoverage::Status)

namespace zylann::voxel {

inline void CollisionSegmentCoverage::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_status"), &CollisionSegmentCoverage::get_status);
	ClassDB::bind_method(D_METHOD("get_leaf_count"), &CollisionSegmentCoverage::get_leaf_count);
	BIND_ENUM_CONSTANT(COLLISION_SEGMENT_READY);
	BIND_ENUM_CONSTANT(COLLISION_SEGMENT_UNAVAILABLE);
	BIND_ENUM_CONSTANT(COLLISION_SEGMENT_OVER_BUDGET);
	BIND_ENUM_CONSTANT(COLLISION_SEGMENT_INVALID);
	BIND_ENUM_CONSTANT(COLLISION_SEGMENT_UNSUPPORTED);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "status"), "", "get_status");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "leaf_count"), "", "get_leaf_count");
}

} // namespace zylann::voxel

#endif // VOXEL_COLLISION_SEGMENT_COVERAGE_H
