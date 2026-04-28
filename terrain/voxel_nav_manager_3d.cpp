#include "voxel_nav_manager_3d.h"

#include "../engine/voxel_engine.h"
#include "../constants/voxel_string_names.h"
#include "../generators/voxel_generator.h"
#include "../meshers/mesh_block_task.h"
#include "../storage/voxel_buffer.h"
#include "../storage/voxel_data.h"
#include "../storage/voxel_format.h"
#include "../util/containers/container_funcs.h"
#include "../util/godot/classes/time.h"
#include "../util/godot/core/callable_mp.h"
#include "../util/godot/core/class_db.h"
#include "../util/godot/object_weak_ref.h"
#include "../util/io/log.h"
#include "../util/math/conv.h"
#include "../util/math/box3i.h"
#include "../util/math/funcs.h"
#include "../util/string/format.h"
#include "../util/tasks/threaded_task.h"
#include "variable_lod/voxel_lod_terrain.h"
#include "voxel_mesh_block.h"
#include "voxel_nav_region_3d.h"
#include <algorithm>
#include <functional>

namespace zylann::voxel {

namespace {

constexpr uint64_t MAX_REGIONS_TO_CREATE = 65536;
constexpr int COARSE_OCCUPANCY_TILE_REGIONS_PER_AXIS = 4;
constexpr int FINE_OCCUPANCY_TILE_REGIONS_PER_AXIS = 1;
constexpr int COARSE_OCCUPANCY_TARGET_SAMPLES_PER_AXIS = 9;
constexpr int FINE_OCCUPANCY_TARGET_SAMPLES_PER_AXIS = 5;
constexpr int NAV_SOURCE_BORDER_BLOCKS = 1;
constexpr uint64_t SLOW_OCCUPANCY_TASK_LOG_THRESHOLD_MSEC = 100;
constexpr uint64_t SLOW_NAV_BAKE_MAIN_THREAD_STEP_LOG_THRESHOLD_MSEC = 16;
constexpr const char *GENERATED_REGION_META_NAME = "voxel_nav_manager_generated";
constexpr float NAV_STITCH_VERTEX_EPSILON = 0.01f;
constexpr float NAV_STITCH_EDGE_EPSILON = 0.05f;
constexpr float NAV_STITCH_FACTOR_EPSILON = 0.0001f;

inline uint64_t get_ticks_msec() {
	return Time::get_singleton()->get_ticks_msec();
}

inline float get_axis(const Vector3 &v, const int axis) {
	return v[axis];
}

inline void set_axis(Vector3 &v, const int axis, const float value) {
	v[axis] = value;
}

bool is_same_point(const Vector3 &a, const Vector3 &b) {
	return a.distance_squared_to(b) <= NAV_STITCH_VERTEX_EPSILON * NAV_STITCH_VERTEX_EPSILON;
}

bool is_on_plane(const Vector3 &v, const int axis, const float coord) {
	return Math::abs(get_axis(v, axis) - coord) <= NAV_STITCH_EDGE_EPSILON;
}

bool get_segment_factor(const Vector3 &a, const Vector3 &b, const Vector3 &p, float &out_t) {
	const Vector3 ab = b - a;
	const float len2 = ab.length_squared();
	if (len2 <= NAV_STITCH_VERTEX_EPSILON * NAV_STITCH_VERTEX_EPSILON) {
		return false;
	}

	const float t = ab.dot(p - a) / len2;
	if (t <= NAV_STITCH_FACTOR_EPSILON || t >= 1.f - NAV_STITCH_FACTOR_EPSILON) {
		return false;
	}

	const Vector3 closest = a + ab * t;
	if (closest.distance_squared_to(p) > NAV_STITCH_EDGE_EPSILON * NAV_STITCH_EDGE_EPSILON) {
		return false;
	}

	out_t = t;
	return true;
}

int find_or_add_vertex(PackedVector3Array &vertices, const Vector3 &position) {
	for (int i = 0; i < vertices.size(); ++i) {
		if (is_same_point(vertices[i], position)) {
			return i;
		}
	}

	const int index = vertices.size();
	vertices.resize(index + 1);
	vertices.ptrw()[index] = position;
	return index;
}

struct EdgeSplitPoint {
	float factor = 0.f;
	Vector3 position;
};

struct BoundaryStitchMesh {
	PackedVector3Array vertices;
	StdVector<PackedInt32Array> polygons;
};

BoundaryStitchMesh get_stitch_mesh(Ref<NavigationMesh> navigation_mesh) {
	BoundaryStitchMesh mesh;
	if (navigation_mesh.is_null()) {
		return mesh;
	}

	mesh.vertices = navigation_mesh->get_vertices();
	const int polygon_count = navigation_mesh->get_polygon_count();
	mesh.polygons.reserve(polygon_count);
	for (int i = 0; i < polygon_count; ++i) {
		mesh.polygons.push_back(navigation_mesh->get_polygon(i));
	}
	return mesh;
}

void apply_stitch_mesh(Ref<NavigationMesh> navigation_mesh, const BoundaryStitchMesh &mesh) {
	ERR_FAIL_COND(navigation_mesh.is_null());

	navigation_mesh->set_vertices(mesh.vertices);
	navigation_mesh->clear_polygons();
	for (const PackedInt32Array &polygon : mesh.polygons) {
		navigation_mesh->add_polygon(polygon);
	}
}

void push_unique_stitch_point(StdVector<Vector3> &points, const Vector3 &point) {
	for (const Vector3 &existing_point : points) {
		if (is_same_point(existing_point, point)) {
			return;
		}
	}
	points.push_back(point);
}

StdVector<Vector3> collect_neighbor_boundary_edge_vertices(
		const BoundaryStitchMesh &neighbor_mesh,
		const int axis,
		const float neighbor_boundary_coord,
		const float local_boundary_coord
) {
	StdVector<Vector3> points;
	for (const PackedInt32Array &polygon : neighbor_mesh.polygons) {
		if (polygon.size() < 2) {
			continue;
		}

		for (int i = 0; i < polygon.size(); ++i) {
			Vector3 a = neighbor_mesh.vertices[polygon[i]];
			Vector3 b = neighbor_mesh.vertices[polygon[(i + 1) % polygon.size()]];
			if (!is_on_plane(a, axis, neighbor_boundary_coord) || !is_on_plane(b, axis, neighbor_boundary_coord)) {
				continue;
			}

			set_axis(a, axis, local_boundary_coord);
			set_axis(b, axis, local_boundary_coord);
			push_unique_stitch_point(points, a);
			push_unique_stitch_point(points, b);
		}
	}
	return points;
}

bool split_boundary_edges(
		BoundaryStitchMesh &mesh,
		const StdVector<Vector3> &split_points,
		const int axis,
		const float boundary_coord
) {
	bool changed = false;

	for (PackedInt32Array &polygon : mesh.polygons) {
		if (polygon.size() < 2) {
			continue;
		}

		bool polygon_changed = false;
		StdVector<int32_t> new_indices;
		new_indices.reserve(polygon.size() + split_points.size());
		for (int i = 0; i < polygon.size(); ++i) {
			const int32_t index_a = polygon[i];
			const int32_t index_b = polygon[(i + 1) % polygon.size()];
			Vector3 a = mesh.vertices[index_a];
			Vector3 b = mesh.vertices[index_b];
			new_indices.push_back(index_a);

			if (!is_on_plane(a, axis, boundary_coord) || !is_on_plane(b, axis, boundary_coord)) {
				continue;
			}

			StdVector<EdgeSplitPoint> edge_split_points;
			for (const Vector3 &split_point : split_points) {
				Vector3 projected_split_point = split_point;
				set_axis(projected_split_point, axis, Math::lerp(get_axis(a, axis), get_axis(b, axis), 0.5f));
				float factor = 0.f;
				if (get_segment_factor(a, b, projected_split_point, factor)) {
					const Vector3 edge_split_position = a + (b - a) * factor;
					bool found = false;
					for (const EdgeSplitPoint &existing_split_point : edge_split_points) {
						if (Math::abs(existing_split_point.factor - factor) <= NAV_STITCH_FACTOR_EPSILON) {
							found = true;
							break;
						}
					}
					if (!found) {
						edge_split_points.push_back({ factor, edge_split_position });
					}
				}
			}

			if (edge_split_points.size() == 0) {
				continue;
			}

			std::sort(
					edge_split_points.begin(),
					edge_split_points.end(),
					[](const EdgeSplitPoint &a, const EdgeSplitPoint &b) { return a.factor < b.factor; }
			);
			for (const EdgeSplitPoint &edge_split_point : edge_split_points) {
				new_indices.push_back(find_or_add_vertex(mesh.vertices, edge_split_point.position));
			}
			polygon_changed = true;
		}

		if (polygon_changed) {
			polygon.resize(new_indices.size());
			int32_t *polygon_w = polygon.ptrw();
			for (unsigned int i = 0; i < new_indices.size(); ++i) {
				polygon_w[i] = new_indices[i];
			}
			changed = true;
		}
	}

	return changed;
}

bool stitch_navigation_mesh_pair(
		Ref<NavigationMesh> navigation_mesh_a,
		Ref<NavigationMesh> navigation_mesh_b,
		const int axis,
		const float block_size,
		const bool b_is_positive_neighbor
) {
	if (navigation_mesh_a.is_null() || navigation_mesh_b.is_null()) {
		return false;
	}

	BoundaryStitchMesh mesh_a = get_stitch_mesh(navigation_mesh_a);
	BoundaryStitchMesh mesh_b = get_stitch_mesh(navigation_mesh_b);
	if (mesh_a.polygons.size() == 0 || mesh_b.polygons.size() == 0) {
		return false;
	}

	const float boundary_coord_a = b_is_positive_neighbor ? block_size : 0.f;
	const float boundary_coord_b = b_is_positive_neighbor ? 0.f : block_size;
	const StdVector<Vector3> points_from_b =
			collect_neighbor_boundary_edge_vertices(mesh_b, axis, boundary_coord_b, boundary_coord_a);
	const StdVector<Vector3> points_from_a =
			collect_neighbor_boundary_edge_vertices(mesh_a, axis, boundary_coord_a, boundary_coord_b);

	const bool changed_a = split_boundary_edges(mesh_a, points_from_b, axis, boundary_coord_a);
	const bool changed_b = split_boundary_edges(mesh_b, points_from_a, axis, boundary_coord_b);

	if (changed_a) {
		apply_stitch_mesh(navigation_mesh_a, mesh_a);
	}
	if (changed_b) {
		apply_stitch_mesh(navigation_mesh_b, mesh_b);
	}
	return changed_a || changed_b;
}

unsigned int get_navigation_occupancy_probe_lod(int span_size_voxels, bool fine_occupancy) {
	const int target_samples_per_axis =
			fine_occupancy ? FINE_OCCUPANCY_TARGET_SAMPLES_PER_AXIS : COARSE_OCCUPANCY_TARGET_SAMPLES_PER_AXIS;
	const int desired_step = math::max(1, math::ceildiv(span_size_voxels, target_samples_per_axis - 1));
	const unsigned int previous_power_of_two =
			math::max(1u, math::get_previous_power_of_two_32(static_cast<unsigned int>(desired_step)));
	return math::get_shift_from_power_of_two_32(previous_power_of_two);
}

class SdfView {
public:
	SdfView(const VoxelBuffer &voxels) :
			_size(voxels.get_size()),
			_depth(voxels.get_channel_depth(VoxelBuffer::CHANNEL_SDF)),
			_uniform(voxels.get_channel_compression(VoxelBuffer::CHANNEL_SDF) == VoxelBuffer::COMPRESSION_UNIFORM) {
		if (_uniform) {
			_uniform_value = voxels.get_voxel_f(0, 0, 0, VoxelBuffer::CHANNEL_SDF);
			return;
		}

		switch (_depth) {
			case VoxelBuffer::DEPTH_8_BIT:
				ZN_ASSERT(voxels.get_channel_data_read_only(VoxelBuffer::CHANNEL_SDF, _data8));
				break;

			case VoxelBuffer::DEPTH_16_BIT:
				ZN_ASSERT(voxels.get_channel_data_read_only(VoxelBuffer::CHANNEL_SDF, _data16));
				break;

			case VoxelBuffer::DEPTH_32_BIT:
				ZN_ASSERT(voxels.get_channel_data_read_only(VoxelBuffer::CHANNEL_SDF, _data32));
				break;

			case VoxelBuffer::DEPTH_64_BIT:
				ZN_ASSERT(voxels.get_channel_data_read_only(VoxelBuffer::CHANNEL_SDF, _data64));
				break;

			default:
				ZN_CRASH();
		}
	}

	inline float get(int x, int y, int z) const {
		if (_uniform) {
			return _uniform_value;
		}

		const unsigned int i = Vector3iUtil::get_zxy_index(x, y, z, _size.x, _size.y);
		switch (_depth) {
			case VoxelBuffer::DEPTH_8_BIT:
				return s8_to_snorm(_data8[i]) * constants::QUANTIZED_SDF_8_BITS_SCALE_INV;

			case VoxelBuffer::DEPTH_16_BIT:
				return s16_to_snorm(_data16[i]) * constants::QUANTIZED_SDF_16_BITS_SCALE_INV;

			case VoxelBuffer::DEPTH_32_BIT:
				return _data32[i];

			case VoxelBuffer::DEPTH_64_BIT:
				return _data64[i];

			default:
				ZN_CRASH();
		}
		return 0.f;
	}

private:
	const Vector3i _size;
	const VoxelBuffer::Depth _depth;
	const bool _uniform;
	float _uniform_value = 0.f;
	Span<const int8_t> _data8;
	Span<const int16_t> _data16;
	Span<const float> _data32;
	Span<const double> _data64;
};

} // namespace

class VoxelNavOccupancyTask : public IThreadedTask {
public:
	zylann::godot::ObjectWeakRef<VoxelNavManager3D> manager;
	zylann::godot::ObjectWeakRef<VoxelLodTerrain> terrain;
	Ref<VoxelGenerator> generator;
	VoxelFormat voxel_format;
	StdVector<Vector3i> occupied_block_positions;
	uint32_t rebuild_id = 0;
	Vector3i tile_region_position;
	Vector3i tile_region_size;
	int mesh_block_size = 0;
	unsigned int region_size_blocks = 0;
	bool fine_occupancy = false;
	int skipped_empty_count = 0;
	int checked_region_count = 0;
	Vector3i sample_size;
	uint64_t generate_time_msec = 0;
	uint64_t scan_time_msec = 0;
	uint64_t total_time_msec = 0;

	void run(ThreadedTaskContext &ctx) override {
		ZN_ASSERT_RETURN(generator.is_valid());
		ZN_ASSERT_RETURN(region_size_blocks > 0);

		const uint64_t time_before = get_ticks_msec();
		checked_region_count = static_cast<int>(Vector3iUtil::get_volume_u64(tile_region_size));
		occupied_block_positions.reserve(checked_region_count);

		const int region_size_voxels = mesh_block_size * static_cast<int>(region_size_blocks);
		const Vector3i tile_size_voxels = tile_region_size * region_size_voxels;
		const unsigned int probe_lod = get_navigation_occupancy_probe_lod(
				fine_occupancy ? mesh_block_size : region_size_voxels,
				fine_occupancy
		);
		const int probe_voxel_step = 1 << probe_lod;
		sample_size = Vector3i(
				math::max(2, math::ceildiv(tile_size_voxels.x, probe_voxel_step) + 1),
				math::max(2, math::ceildiv(tile_size_voxels.y, probe_voxel_step) + 1),
				math::max(2, math::ceildiv(tile_size_voxels.z, probe_voxel_step) + 1)
		);
		const Vector3i tile_origin_voxels = tile_region_position * region_size_voxels;
		const float surface_threshold = 1.f;

		VoxelBuffer voxels(VoxelBuffer::ALLOCATOR_POOL);
		voxels.create(sample_size, &voxel_format);
		VoxelGenerator::VoxelQueryData query{ voxels, tile_origin_voxels, probe_lod };
		generator->generate_block(query);
		const uint64_t time_after_generate = get_ticks_msec();
		generate_time_msec = time_after_generate - time_before;
		const SdfView sdf_view(voxels);

		for (int rz = 0; rz < tile_region_size.z; ++rz) {
			for (int ry = 0; ry < tile_region_size.y; ++ry) {
				for (int rx = 0; rx < tile_region_size.x; ++rx) {
					const Vector3i region_offset(rx, ry, rz);
					const Vector3i sample_min = (region_offset * region_size_voxels) >> probe_lod;
					const Vector3i sample_max = Vector3i(
							math::min(sample_size.x, math::ceildiv((rx + 1) * region_size_voxels, probe_voxel_step) + 1),
							math::min(sample_size.y, math::ceildiv((ry + 1) * region_size_voxels, probe_voxel_step) + 1),
							math::min(sample_size.z, math::ceildiv((rz + 1) * region_size_voxels, probe_voxel_step) + 1)
					);

					bool has_positive = false;
					bool has_negative = false;
					bool has_surface = false;
					for (int sz = sample_min.z; sz < sample_max.z && !has_surface; ++sz) {
						for (int sx = sample_min.x; sx < sample_max.x && !has_surface; ++sx) {
							for (int sy = sample_min.y; sy < sample_max.y; ++sy) {
								const float sdf = sdf_view.get(sx, sy, sz);
								if (Math::abs(sdf) <= surface_threshold) {
									has_surface = true;
									break;
								}
								if (sdf > 0.f) {
									has_positive = true;
								} else {
									has_negative = true;
								}
								if (has_positive && has_negative) {
									has_surface = true;
									break;
								}
							}
						}
					}

					if (has_surface) {
						occupied_block_positions.push_back((tile_region_position + region_offset) * region_size_blocks);
					} else {
						++skipped_empty_count;
					}
				}
			}
		}
		const uint64_t time_after_scan = get_ticks_msec();
		scan_time_msec = time_after_scan - time_after_generate;
		total_time_msec = time_after_scan - time_before;
	}

	void apply_result() override {
		const uint64_t apply_time_before = get_ticks_msec();
		VoxelNavManager3D *manager_ptr = manager.get();
		if (manager_ptr == nullptr) {
			return;
		}
		if (manager_ptr->_region_rebuild_id != rebuild_id || !manager_ptr->_region_rebuild_in_progress) {
			return;
		}

		manager_ptr->_region_probe_checked_count += checked_region_count;
		manager_ptr->_region_probe_generate_time_msec += generate_time_msec;
		manager_ptr->_region_probe_scan_time_msec += scan_time_msec;
		manager_ptr->_region_probe_max_task_time_msec =
				math::max(manager_ptr->_region_probe_max_task_time_msec, total_time_msec);
		if (total_time_msec >= SLOW_OCCUPANCY_TASK_LOG_THRESHOLD_MSEC) {
			++manager_ptr->_region_probe_slow_task_count;
			zylann::print_line(format(
					"VoxelNavManager3D: slow occupancy tile pos=({}, {}, {}) size=({}, {}, {}) samples=({}, {}, {}) checked={} occupied={} generate={} ms scan={} ms total={} ms",
					tile_region_position.x,
					tile_region_position.y,
					tile_region_position.z,
					tile_region_size.x,
					tile_region_size.y,
					tile_region_size.z,
					sample_size.x,
					sample_size.y,
					sample_size.z,
					checked_region_count,
					occupied_block_positions.size(),
					generate_time_msec,
					scan_time_msec,
					total_time_msec
			));
		}

		VoxelLodTerrain *terrain_ptr = terrain.get();
		if (terrain_ptr == nullptr) {
			manager_ptr->_region_probe_skipped_empty_count += checked_region_count;
		} else {
			manager_ptr->_region_probe_skipped_empty_count += skipped_empty_count;

			for (const Vector3i block_position : occupied_block_positions) {
				VoxelNavRegion3D *region = manager_ptr->create_region(*terrain_ptr, block_position);
				if (region != nullptr) {
					manager_ptr->_regions.push_back(region);
				} else {
					++manager_ptr->_region_probe_failed_create_count;
				}
			}
		}
		manager_ptr->_region_probe_apply_time_msec += get_ticks_msec() - apply_time_before;

		--manager_ptr->_pending_region_probe_task_count;
		if (manager_ptr->_pending_region_probe_task_count == 0) {
			manager_ptr->_region_rebuild_in_progress = false;
			if (manager_ptr->_region_probe_failed_create_count > 0) {
				zylann::print_line(format(
						"VoxelNavManager3D: skipped {} occupied region candidate(s) that could not be initialized",
						manager_ptr->_region_probe_failed_create_count
				));
			}
			if (manager_ptr->_region_probe_skipped_empty_count > 0) {
				zylann::print_line(format(
						"VoxelNavManager3D: skipped {} / {} empty region(s) by {} threaded occupancy probe",
						manager_ptr->_region_probe_skipped_empty_count,
						manager_ptr->_region_probe_checked_count,
						manager_ptr->_fine_occupancy_enabled ? "fine" : "coarse"
				));
			}
			zylann::print_line(format(
					"VoxelNavManager3D: occupancy summary tasks={}, checked={}, occupied={}, skipped_empty={}, slow_tasks={}, worker_generate={} ms, worker_scan={} ms, main_apply={} ms, max_task={} ms",
					manager_ptr->_region_probe_task_count,
					manager_ptr->_region_probe_checked_count,
					manager_ptr->_region_probe_checked_count - manager_ptr->_region_probe_skipped_empty_count,
					manager_ptr->_region_probe_skipped_empty_count,
					manager_ptr->_region_probe_slow_task_count,
					manager_ptr->_region_probe_generate_time_msec,
					manager_ptr->_region_probe_scan_time_msec,
					manager_ptr->_region_probe_apply_time_msec,
					manager_ptr->_region_probe_max_task_time_msec
			));
			zylann::print_line(format(
					"VoxelNavManager3D: rebuilt {} navigation region(s)",
					manager_ptr->_regions.size()
			));

			if (manager_ptr->_bake_requested_after_region_rebuild) {
				manager_ptr->_bake_requested_after_region_rebuild = false;
				if (manager_ptr->_regions.size() > 0) {
					manager_ptr->bake_navigation_meshes();
				} else {
					zylann::print_line("VoxelNavManager3D: no occupied navigation region(s) to bake");
				}
			}
		}
	}

	const char *get_debug_name() const override {
		return "VoxelNavOccupancy";
	}
};

class VoxelNavSourceMeshTask : public IThreadedTask {
public:
	zylann::godot::ObjectWeakRef<VoxelNavManager3D> manager;
	zylann::godot::ObjectWeakRef<VoxelNavRegion3D> region;
	zylann::godot::ObjectWeakRef<VoxelLodTerrain> terrain;
	std::shared_ptr<VoxelData> data;
	Ref<VoxelMesher> mesher;
	Ref<VoxelGenerator> generator;
	Vector3i region_block_position;
	uint32_t generation_id = 0;
	int mesh_block_size = 0;
	int region_size_blocks = 0;
	PackedVector3Array vertices;
	PackedInt32Array indices;
	uint64_t worker_time_msec = 0;
	int meshed_block_count = 0;
	int empty_block_count = 0;

	void run(ThreadedTaskContext &ctx) override {
		ZN_ASSERT_RETURN(data != nullptr);
		ZN_ASSERT_RETURN(mesher.is_valid());
		ZN_ASSERT_RETURN(region_size_blocks > 0);

		const uint64_t time_before = get_ticks_msec();
		for (int z = -NAV_SOURCE_BORDER_BLOCKS; z < region_size_blocks + NAV_SOURCE_BORDER_BLOCKS; ++z) {
			for (int y = -NAV_SOURCE_BORDER_BLOCKS; y < region_size_blocks + NAV_SOURCE_BORDER_BLOCKS; ++y) {
				for (int x = -NAV_SOURCE_BORDER_BLOCKS; x < region_size_blocks + NAV_SOURCE_BORDER_BLOCKS; ++x) {
					const Vector3i block_offset(x, y, z);
					const Vector3i block_position = region_block_position + block_offset;

					VoxelMesher::Output output;
					if (!build_mesh_block_output(
								output,
								*data,
								mesher,
								generator,
								block_position,
								mesh_block_size,
								0,
								true,
								false
						)) {
						++empty_block_count;
						continue;
					}

					PackedVector3Array block_vertices;
					PackedInt32Array block_indices;
					if (!get_collision_mesh_from_mesher_output(output, **mesher, block_vertices, block_indices)) {
						++empty_block_count;
						continue;
					}

					const int vertex_offset = vertices.size();
					const int previous_vertex_count = vertices.size();
					const int previous_index_count = indices.size();
					vertices.resize(previous_vertex_count + block_vertices.size());
					indices.resize(previous_index_count + block_indices.size());

					const Vector3 local_offset = to_vec3(block_offset * mesh_block_size);
					Vector3 *vertices_w = vertices.ptrw();
					const Vector3 *block_vertices_r = block_vertices.ptr();
					for (int i = 0; i < block_vertices.size(); ++i) {
						vertices_w[previous_vertex_count + i] = block_vertices_r[i] + local_offset;
					}

					int32_t *indices_w = indices.ptrw();
					const int32_t *block_indices_r = block_indices.ptr();
					for (int i = 0; i < block_indices.size(); ++i) {
						indices_w[previous_index_count + i] = block_indices_r[i] + vertex_offset;
					}

					++meshed_block_count;
				}
			}
		}
		worker_time_msec = get_ticks_msec() - time_before;
	}

	void apply_result() override {
		const uint64_t apply_time_before = get_ticks_msec();
		VoxelNavManager3D *manager_ptr = manager.get();
		if (manager_ptr == nullptr) {
			return;
		}
		if (manager_ptr->_nav_source_generation_id != generation_id || !manager_ptr->_nav_source_generation_in_progress) {
			return;
		}

		manager_ptr->_nav_source_worker_time_msec += worker_time_msec;
		manager_ptr->_nav_source_meshed_block_count += meshed_block_count;
		manager_ptr->_nav_source_empty_block_count += empty_block_count;
		manager_ptr->_nav_source_max_task_time_msec =
				math::max(manager_ptr->_nav_source_max_task_time_msec, worker_time_msec);

		VoxelNavRegion3D *region_ptr = region.get();
		const bool has_source = vertices.size() >= 3 && indices.size() >= 3;
		if (!has_source) {
			++manager_ptr->_nav_source_empty_count;
		} else {
			if (region_ptr == nullptr) {
				VoxelLodTerrain *terrain_ptr = terrain.get();
				if (terrain_ptr != nullptr) {
					region_ptr = manager_ptr->create_region(*terrain_ptr, region_block_position);
					if (region_ptr != nullptr) {
						manager_ptr->_regions.push_back(region_ptr);
					}
				}
			}
			if (region_ptr == nullptr) {
				++manager_ptr->_nav_source_empty_count;
			} else {
				region_ptr->set_source_geometry_from_collision_arrays(vertices, indices, worker_time_msec);
			}
		}
		if (region_ptr != nullptr) {
			manager_ptr->_pending_bake_regions.push_back(region_ptr);
		}
		manager_ptr->_nav_source_apply_time_msec += get_ticks_msec() - apply_time_before;

		--manager_ptr->_pending_nav_source_task_count;
		if (manager_ptr->_pending_nav_source_task_count == 0) {
			manager_ptr->_nav_source_generation_in_progress = false;
			zylann::print_line(format(
					"VoxelNavManager3D: source mesh summary tasks={}, empty_regions={}, meshed_blocks={}, empty_blocks={}, worker_total={} ms, apply_total={} ms, max_task={} ms",
					manager_ptr->_nav_source_task_count,
					manager_ptr->_nav_source_empty_count,
					manager_ptr->_nav_source_meshed_block_count,
					manager_ptr->_nav_source_empty_block_count,
					manager_ptr->_nav_source_worker_time_msec,
					manager_ptr->_nav_source_apply_time_msec,
					manager_ptr->_nav_source_max_task_time_msec
			));
			manager_ptr->bake_prebuilt_navigation_meshes(to_span(manager_ptr->_pending_bake_regions));
		}
	}

	const char *get_debug_name() const override {
		return "VoxelNavSourceMesh";
	}
};

class VoxelNavBakeTask : public IThreadedTask {
public:
	zylann::godot::ObjectWeakRef<VoxelNavManager3D> manager;
	zylann::godot::ObjectWeakRef<VoxelNavRegion3D> region;
	Ref<NavigationMesh> navigation_mesh;
	Ref<NavigationMeshSourceGeometryData3D> source_geometry_data;
	uint32_t bake_generation_id = 0;
	uint64_t bake_time_msec = 0;

	void run(ThreadedTaskContext &ctx) override {
		ZN_ASSERT_RETURN(navigation_mesh.is_valid());
		ZN_ASSERT_RETURN(source_geometry_data.is_valid());

		const uint64_t time_before = get_ticks_msec();
		NavigationServer3D::get_singleton()->bake_from_source_geometry_data(navigation_mesh, source_geometry_data);
		bake_time_msec = get_ticks_msec() - time_before;
	}

	void apply_result() override {
		VoxelNavManager3D *manager_ptr = manager.get();
		if (manager_ptr == nullptr) {
			return;
		}
		if (manager_ptr->_nav_bake_generation_id != bake_generation_id || !manager_ptr->_nav_bake_in_progress) {
			return;
		}

		VoxelNavRegion3D *region_ptr = region.get();
		if (region_ptr != nullptr && region_ptr->is_inside_tree()) {
			region_ptr->set_last_navigation_bake_time_msec(bake_time_msec);
			const uint64_t apply_time_before = get_ticks_msec();
			region_ptr->apply_baked_navigation_mesh(navigation_mesh);
			const uint64_t apply_time_msec = get_ticks_msec() - apply_time_before;
			manager_ptr->_nav_bake_apply_total_time_msec += apply_time_msec;
			manager_ptr->_nav_bake_apply_max_time_msec =
					math::max(manager_ptr->_nav_bake_apply_max_time_msec, apply_time_msec);

			const int polygon_count = navigation_mesh->get_polygon_count();
			if (polygon_count > 0) {
				++manager_ptr->_nav_bake_baked_count;
				manager_ptr->_nav_bake_polygon_count += polygon_count;
			}
			manager_ptr->_nav_bake_total_time_msec += bake_time_msec;
			manager_ptr->_nav_bake_max_time_msec = math::max(manager_ptr->_nav_bake_max_time_msec, bake_time_msec);
			if (apply_time_msec >= SLOW_NAV_BAKE_MAIN_THREAD_STEP_LOG_THRESHOLD_MSEC) {
				const Vector3i block_position = region_ptr->get_block_position();
				zylann::print_line(format(
						"VoxelNavManager3D: slow bake apply region block=({}, {}, {}) polygons={} apply={} ms nav_server_worker={} ms",
						block_position.x,
						block_position.y,
						block_position.z,
						polygon_count,
						apply_time_msec,
						bake_time_msec
				));
			}
			if (bake_time_msec >= 500) {
				const Vector3i block_position = region_ptr->get_block_position();
				zylann::print_line(format(
						"VoxelNavManager3D: slow async bake region block=({}, {}, {}) polygons={} source_mesh={} ms source_geometry={} ms nav_server={} ms total={} ms",
						block_position.x,
						block_position.y,
						block_position.z,
						polygon_count,
						region_ptr->get_last_source_mesh_time_msec(),
						region_ptr->get_last_source_geometry_time_msec(),
						region_ptr->get_last_navigation_bake_time_msec(),
						bake_time_msec
				));
			}
		}

		--manager_ptr->_pending_nav_bake_task_count;
		if (manager_ptr->_pending_nav_bake_task_count == 0) {
			manager_ptr->finish_navigation_bake_batch();
		}
	}

	const char *get_debug_name() const override {
		return "VoxelNavBake";
	}
};

VoxelNavManager3D::VoxelNavManager3D() {}

void VoxelNavManager3D::set_nav_mesh_settings(Ref<VoxelNavMeshSettings> settings) {
	_nav_mesh_settings = settings;
}

Ref<VoxelNavMeshSettings> VoxelNavManager3D::get_nav_mesh_settings() const {
	return _nav_mesh_settings;
}

void VoxelNavManager3D::set_bake_on_ready(bool enabled) {
	_bake_on_ready = enabled;
}

bool VoxelNavManager3D::get_bake_on_ready() const {
	return _bake_on_ready;
}

void VoxelNavManager3D::set_navigation_layers(uint32_t navigation_layers) {
	if (_navigation_layers == navigation_layers) {
		return;
	}

	_navigation_layers = navigation_layers;

	for (VoxelNavRegion3D *region : _regions) {
		if (region != nullptr) {
			region->set_navigation_layers(_navigation_layers);
		}
	}
}

uint32_t VoxelNavManager3D::get_navigation_layers() const {
	return _navigation_layers;
}

void VoxelNavManager3D::set_navigation_layer_value(int layer_number, bool value) {
	ERR_FAIL_COND_MSG(layer_number < 1, "Navigation layer number must be between 1 and 32 inclusive.");
	ERR_FAIL_COND_MSG(layer_number > 32, "Navigation layer number must be between 1 and 32 inclusive.");

	uint32_t navigation_layers = get_navigation_layers();

	if (value) {
		navigation_layers |= 1 << (layer_number - 1);
	} else {
		navigation_layers &= ~(1 << (layer_number - 1));
	}

	set_navigation_layers(navigation_layers);
}

bool VoxelNavManager3D::get_navigation_layer_value(int layer_number) const {
	ERR_FAIL_COND_V_MSG(layer_number < 1, false, "Navigation layer number must be between 1 and 32 inclusive.");
	ERR_FAIL_COND_V_MSG(layer_number > 32, false, "Navigation layer number must be between 1 and 32 inclusive.");

	return get_navigation_layers() & (1 << (layer_number - 1));
}

void VoxelNavManager3D::set_region_size_power(int power) {
	ERR_FAIL_COND(power < 0);
	ERR_FAIL_COND(power > 8);
	_region_size_power = power;
}

int VoxelNavManager3D::get_region_size_power() const {
	return _region_size_power;
}

void VoxelNavManager3D::set_fine_occupancy_enabled(bool enabled) {
	_fine_occupancy_enabled = enabled;
}

bool VoxelNavManager3D::is_fine_occupancy_enabled() const {
	return _fine_occupancy_enabled;
}

void VoxelNavManager3D::rebuild_regions() {
	refresh_terrain_connections();
	++_region_rebuild_id;
	_region_rebuild_in_progress = false;
	_bake_requested_after_region_rebuild = false;
	_pending_region_probe_task_count = 0;
	_region_probe_task_count = 0;
	_region_probe_checked_count = 0;
	_region_probe_skipped_empty_count = 0;
	_region_probe_failed_create_count = 0;
	_region_probe_generate_time_msec = 0;
	_region_probe_scan_time_msec = 0;
	_region_probe_apply_time_msec = 0;
	_region_probe_max_task_time_msec = 0;
	_region_probe_slow_task_count = 0;
	++_nav_source_generation_id;
	_nav_source_generation_in_progress = false;
	_pending_nav_source_task_count = 0;
	_nav_source_task_count = 0;
	_nav_source_empty_count = 0;
	_nav_source_meshed_block_count = 0;
	_nav_source_empty_block_count = 0;
	_nav_source_worker_time_msec = 0;
	_nav_source_apply_time_msec = 0;
	_nav_source_max_task_time_msec = 0;
	++_nav_bake_generation_id;
	_nav_bake_in_progress = false;
	_pending_nav_bake_task_count = 0;
	_nav_bake_task_count = 0;
	_nav_bake_baked_count = 0;
	_nav_bake_polygon_count = 0;
	_nav_bake_removed_empty_region_count = 0;
	_nav_bake_total_time_msec = 0;
	_nav_bake_max_time_msec = 0;
	_nav_bake_apply_total_time_msec = 0;
	_nav_bake_apply_max_time_msec = 0;
	_nav_bake_finalize_time_msec = 0;
	_nav_bake_cleanup_time_msec = 0;
	_nav_bake_stitch_time_msec = 0;
	_nav_bake_wall_time_before_msec = 0;
	_pending_bake_regions.clear();

	clear_regions_internal();

	StdVector<VoxelLodTerrain *> terrains;
	collect_voxel_lod_terrains(this, terrains);
	zylann::print_line(format("VoxelNavManager3D: found {} VoxelLodTerrain node(s)", terrains.size()));

	StdVector<IThreadedTask *> tasks;
	const uint32_t rebuild_id = _region_rebuild_id;

	for (VoxelLodTerrain *terrain : terrains) {
		const int mesh_block_size = terrain->get_mesh_block_size();
		const int region_size_blocks = 1 << _region_size_power;
		const int region_size_voxels = mesh_block_size * region_size_blocks;
		const Box3i voxel_bounds = terrain->get_voxel_bounds();
		const Vector3i region_min = math::floordiv(voxel_bounds.position, region_size_voxels);
		const Vector3i region_max = math::ceildiv(voxel_bounds.position + voxel_bounds.size, region_size_voxels);
		const Box3i region_bounds = Box3i::from_min_max(region_min, region_max);
		const uint64_t region_count = Vector3iUtil::get_volume_u64(region_bounds.size);

		zylann::print_line(
				format("VoxelNavManager3D: terrain voxel bounds pos=({}, {}, {}) size=({}, {}, {}), mesh_block_size={}, region_size_power={}, region_size_voxels={}, region grid pos=({}, {}, {}) size=({}, {}, {})",
					   voxel_bounds.position.x,
					   voxel_bounds.position.y,
					   voxel_bounds.position.z,
					   voxel_bounds.size.x,
					   voxel_bounds.size.y,
					   voxel_bounds.size.z,
					   mesh_block_size,
					   _region_size_power,
					   region_size_voxels,
					   region_bounds.position.x,
					   region_bounds.position.y,
					   region_bounds.position.z,
					   region_bounds.size.x,
					   region_bounds.size.y,
					   region_bounds.size.z)
		);

		if (region_count > MAX_REGIONS_TO_CREATE) {
			ZN_PRINT_WARNING(format(
					"VoxelNavManager3D: skipped terrain because voxel_bounds would create {} regions. Set tighter "
					"voxel_bounds, increase region_size_power, or increase the implementation cap.",
					region_count
			));
			continue;
		}

		Ref<VoxelGenerator> generator = terrain->get_generator();
		if (generator.is_null()) {
			ZN_PRINT_WARNING("VoxelNavManager3D: skipped terrain because it has no generator");
			continue;
		}

		const VoxelFormat voxel_format = terrain->get_storage().get_format();
		const int tile_region_size_axis =
				_fine_occupancy_enabled ? FINE_OCCUPANCY_TILE_REGIONS_PER_AXIS : COARSE_OCCUPANCY_TILE_REGIONS_PER_AXIS;
		const Vector3i region_end = region_bounds.position + region_bounds.size;

		for (int z = region_bounds.position.z; z < region_end.z; z += tile_region_size_axis) {
			for (int y = region_bounds.position.y; y < region_end.y; y += tile_region_size_axis) {
				for (int x = region_bounds.position.x; x < region_end.x; x += tile_region_size_axis) {
					const Vector3i tile_region_position(x, y, z);
					const Vector3i tile_region_size(
							math::min(tile_region_size_axis, region_end.x - x),
							math::min(tile_region_size_axis, region_end.y - y),
							math::min(tile_region_size_axis, region_end.z - z)
					);

					VoxelNavOccupancyTask *task = ZN_NEW(VoxelNavOccupancyTask);
					task->manager.set(this);
					task->terrain.set(terrain);
					task->generator = generator;
					task->voxel_format = voxel_format;
					task->tile_region_position = tile_region_position;
					task->tile_region_size = tile_region_size;
					task->rebuild_id = rebuild_id;
					task->mesh_block_size = mesh_block_size;
					task->region_size_blocks = region_size_blocks;
					task->fine_occupancy = _fine_occupancy_enabled;
					tasks.push_back(task);
				}
			}
		}
	}

	if (tasks.size() == 0) {
		zylann::print_line("VoxelNavManager3D: rebuilt 0 navigation region(s)");
		return;
	}

	_region_rebuild_in_progress = true;
	_pending_region_probe_task_count = tasks.size();
	_region_probe_task_count = tasks.size();
	zylann::print_line(format(
			"VoxelNavManager3D: scheduled {} threaded {} occupancy tile task(s), tile_regions_per_axis={}. If this line is absent before baking, existing region nodes were reused and occupancy did not run.",
			tasks.size(),
			_fine_occupancy_enabled ? "fine" : "coarse",
			_fine_occupancy_enabled ? FINE_OCCUPANCY_TILE_REGIONS_PER_AXIS : COARSE_OCCUPANCY_TILE_REGIONS_PER_AXIS
	));
	VoxelEngine::get_singleton().push_async_tasks(to_span(tasks));
}

void VoxelNavManager3D::bake_navigation_meshes() {
	if (_nav_source_generation_in_progress) {
		zylann::print_line("VoxelNavManager3D: nav source mesh generation is already in progress");
		return;
	}
	if (_nav_bake_in_progress) {
		zylann::print_line("VoxelNavManager3D: navigation baking is already in progress");
		return;
	}

	if (_region_rebuild_in_progress) {
		_bake_requested_after_region_rebuild = true;
		zylann::print_line("VoxelNavManager3D: delaying navmesh bake until threaded region rebuild completes");
		return;
	}

	if (_regions.size() == 0) {
		rebuild_regions();
		if (_region_rebuild_in_progress) {
			_bake_requested_after_region_rebuild = true;
			zylann::print_line("VoxelNavManager3D: delaying navmesh bake until threaded region rebuild completes");
			return;
		}
	}

	schedule_navigation_source_mesh_tasks();
}

void VoxelNavManager3D::schedule_navigation_source_mesh_tasks() {
	StdVector<ManagedRegionKey> region_keys;
	region_keys.reserve(_regions.size());
	for (VoxelNavRegion3D *region : _regions) {
		if (region == nullptr) {
			continue;
		}
		VoxelLodTerrain *terrain = region->get_terrain();
		if (terrain == nullptr) {
			continue;
		}
		region_keys.push_back({ terrain, region->get_block_position() });
	}
	schedule_navigation_source_mesh_tasks_for_regions(to_span_const(region_keys));
}

void VoxelNavManager3D::schedule_navigation_source_mesh_tasks_for_regions(Span<const ManagedRegionKey> region_keys) {
	++_nav_source_generation_id;
	_nav_source_generation_in_progress = false;
	++_nav_bake_generation_id;
	_nav_bake_in_progress = false;
	_pending_nav_source_task_count = 0;
	_nav_source_task_count = 0;
	_nav_source_empty_count = 0;
	_nav_source_meshed_block_count = 0;
	_nav_source_empty_block_count = 0;
	_nav_source_worker_time_msec = 0;
	_nav_source_apply_time_msec = 0;
	_nav_source_max_task_time_msec = 0;
	_pending_nav_bake_task_count = 0;
	_nav_bake_task_count = 0;
	_nav_bake_baked_count = 0;
	_nav_bake_polygon_count = 0;
	_nav_bake_removed_empty_region_count = 0;
	_nav_bake_total_time_msec = 0;
	_nav_bake_max_time_msec = 0;
	_nav_bake_apply_total_time_msec = 0;
	_nav_bake_apply_max_time_msec = 0;
	_nav_bake_finalize_time_msec = 0;
	_nav_bake_cleanup_time_msec = 0;
	_nav_bake_stitch_time_msec = 0;
	_nav_bake_wall_time_before_msec = 0;
	_pending_bake_regions.clear();

	StdVector<IThreadedTask *> tasks;
	const uint32_t generation_id = _nav_source_generation_id;
	const int region_size_blocks = 1 << _region_size_power;

	for (const ManagedRegionKey &region_key : region_keys) {
		VoxelLodTerrain *terrain = region_key.terrain;
		if (terrain == nullptr || !terrain->is_inside_tree()) {
			continue;
		}
		VoxelNavRegion3D *region = find_region(*terrain, region_key.block_position);
		if (region != nullptr) {
			if (!region->is_inside_tree()) {
				continue;
			}
			region->clear_source_geometry();
		}
		Ref<VoxelMesher> mesher = terrain->get_mesher();
		if (mesher.is_null()) {
			++_nav_source_empty_count;
			if (region != nullptr) {
				_pending_bake_regions.push_back(region);
			}
			continue;
		}
		std::shared_ptr<VoxelData> data = terrain->get_storage_shared();
		if (data == nullptr) {
			++_nav_source_empty_count;
			if (region != nullptr) {
				_pending_bake_regions.push_back(region);
			}
			continue;
		}

		VoxelNavSourceMeshTask *task = ZN_NEW(VoxelNavSourceMeshTask);
		task->manager.set(this);
		if (region != nullptr) {
			task->region.set(region);
		}
		task->terrain.set(terrain);
		task->data = data;
		task->mesher = mesher;
		task->generator = terrain->get_generator();
		task->region_block_position = region_key.block_position;
		task->generation_id = generation_id;
		task->mesh_block_size = terrain->get_mesh_block_size();
		task->region_size_blocks = region_size_blocks;
		tasks.push_back(task);
	}

	if (tasks.size() == 0) {
		bake_prebuilt_navigation_meshes(to_span(_pending_bake_regions));
		return;
	}

	_nav_source_generation_in_progress = true;
	_pending_nav_source_task_count = tasks.size();
	_nav_source_task_count = tasks.size();
	zylann::print_line(format(
			"VoxelNavManager3D: scheduled {} threaded nav source mesh task(s), region_size_blocks={}",
			tasks.size(),
			region_size_blocks
	));
	VoxelEngine::get_singleton().push_async_tasks(to_span(tasks));
}

void VoxelNavManager3D::bake_prebuilt_navigation_meshes(Span<VoxelNavRegion3D *> regions) {
	if (_nav_source_generation_in_progress || _nav_bake_in_progress) {
		return;
	}

	++_nav_bake_generation_id;
	_pending_nav_bake_task_count = 0;
	_nav_bake_task_count = 0;
	_nav_bake_baked_count = 0;
	_nav_bake_polygon_count = 0;
	_nav_bake_removed_empty_region_count = 0;
	_nav_bake_total_time_msec = 0;
	_nav_bake_max_time_msec = 0;
	_nav_bake_apply_total_time_msec = 0;
	_nav_bake_apply_max_time_msec = 0;
	_nav_bake_finalize_time_msec = 0;
	_nav_bake_cleanup_time_msec = 0;
	_nav_bake_stitch_time_msec = 0;
	_nav_bake_wall_time_before_msec = get_ticks_msec();

	zylann::print_line(format("VoxelNavManager3D: scheduling async bake for {} navigation region(s)", regions.size()));
	StdVector<IThreadedTask *> tasks;
	const uint32_t bake_generation_id = _nav_bake_generation_id;
	for (unsigned int region_index = 0; region_index < regions.size(); ++region_index) {
		VoxelNavRegion3D *region = regions[region_index];
		if (region != nullptr && region->is_inside_tree()) {
			if (region_index == 0 || (region_index % 16) == 0) {
				const Vector3i block_position = region->get_block_position();
				zylann::print_line(
						format("VoxelNavManager3D: scheduling bake region {} / {} at block ({}, {}, {})",
							   region_index + 1,
							   regions.size(),
							   block_position.x,
							   block_position.y,
							   block_position.z)
				);
			}

			if (!region->has_source_geometry()) {
				continue;
			}
			VoxelNavBakeTask *task = ZN_NEW(VoxelNavBakeTask);
			task->manager.set(this);
			task->region.set(region);
			task->navigation_mesh = region->create_configured_navigation_mesh();
			task->source_geometry_data = region->_source_geometry_data;
			task->bake_generation_id = bake_generation_id;
			tasks.push_back(task);
		}
	}

	if (tasks.size() == 0) {
		finish_navigation_bake_batch();
		return;
	}

	_nav_bake_in_progress = true;
	_pending_nav_bake_task_count = tasks.size();
	_nav_bake_task_count = tasks.size();
	VoxelEngine::get_singleton().push_async_tasks(to_span(tasks));
}

void VoxelNavManager3D::finish_navigation_bake_batch() {
	if (!_nav_bake_in_progress && _nav_bake_task_count > 0) {
		return;
	}
	const uint64_t finalize_time_before = get_ticks_msec();
	_nav_bake_in_progress = false;

	uint64_t bake_source_mesh_total_time_msec = 0;
	uint64_t bake_source_geometry_total_time_msec = 0;
	uint64_t bake_navigation_server_total_time_msec = 0;
	const uint64_t cleanup_time_before = get_ticks_msec();
	for (unsigned int region_index = 0; region_index < _pending_bake_regions.size(); ++region_index) {
		VoxelNavRegion3D *region = _pending_bake_regions[region_index];
		if (region == nullptr || !region->is_inside_tree()) {
			continue;
		}

		bake_source_mesh_total_time_msec += region->get_last_source_mesh_time_msec();
		bake_source_geometry_total_time_msec += region->get_last_source_geometry_time_msec();
		bake_navigation_server_total_time_msec += region->get_last_navigation_bake_time_msec();

		Ref<NavigationMesh> navigation_mesh = region->get_navigation_mesh();
		if (!region->has_source_geometry() || navigation_mesh.is_null() || navigation_mesh->get_polygon_count() == 0) {
			unregister_region(*region);
			for (VoxelNavRegion3D *&stored_region : _regions) {
				if (stored_region == region) {
					stored_region = nullptr;
					break;
				}
			}
			if (region->get_parent() == this) {
				remove_child(region);
			}
			memdelete(region);
			_pending_bake_regions[region_index] = nullptr;
			++_nav_bake_removed_empty_region_count;
		}
	}
	_nav_bake_cleanup_time_msec = get_ticks_msec() - cleanup_time_before;
	if (_nav_bake_removed_empty_region_count > 0) {
		StdVector<VoxelNavRegion3D *> kept_regions;
		kept_regions.reserve(_regions.size() - _nav_bake_removed_empty_region_count);
		for (VoxelNavRegion3D *region : _regions) {
			if (region != nullptr) {
				kept_regions.push_back(region);
			}
		}
		_regions.swap(kept_regions);
		zylann::print_line(format(
				"VoxelNavManager3D: removed {} empty navigation region candidate(s)",
				_nav_bake_removed_empty_region_count
		));
	}
	const uint64_t stitch_time_before = get_ticks_msec();
	stitch_baked_region_edges(to_span(_pending_bake_regions));
	_nav_bake_stitch_time_msec = get_ticks_msec() - stitch_time_before;
	_nav_bake_finalize_time_msec = get_ticks_msec() - finalize_time_before;
	if (_nav_bake_finalize_time_msec >= SLOW_NAV_BAKE_MAIN_THREAD_STEP_LOG_THRESHOLD_MSEC) {
		zylann::print_line(format(
				"VoxelNavManager3D: slow bake finalize cleanup={} ms stitch={} ms finalize={} ms",
				_nav_bake_cleanup_time_msec,
				_nav_bake_stitch_time_msec,
				_nav_bake_finalize_time_msec
		));
	}
	zylann::print_line(
			format("VoxelNavManager3D: baked {} / {} navigation mesh(es), {} polygon(s), {} region node(s) retained, source_mesh_total={} ms, source_geometry_total={} ms, nav_server_total={} ms, region_bake_total={} ms, region_bake_max={} ms, apply_total={} ms, apply_max={} ms, cleanup={} ms, stitch={} ms, finalize={} ms, wall={} ms",
				   _nav_bake_baked_count,
				   _pending_bake_regions.size(),
				   _nav_bake_polygon_count,
				   _regions.size(),
				   bake_source_mesh_total_time_msec,
				   bake_source_geometry_total_time_msec,
				   bake_navigation_server_total_time_msec,
				   _nav_bake_total_time_msec,
				   _nav_bake_max_time_msec,
				   _nav_bake_apply_total_time_msec,
				   _nav_bake_apply_max_time_msec,
				   _nav_bake_cleanup_time_msec,
				   _nav_bake_stitch_time_msec,
				   _nav_bake_finalize_time_msec,
				   get_ticks_msec() - _nav_bake_wall_time_before_msec)
	);
	_pending_bake_regions.clear();
}

void VoxelNavManager3D::stitch_baked_region_edges(Span<VoxelNavRegion3D *> regions) {
	const int region_size_blocks = 1 << _region_size_power;
	int stitched_pair_count = 0;

	for (VoxelNavRegion3D *region : regions) {
		if (region == nullptr || !region->is_inside_tree()) {
			continue;
		}
		VoxelLodTerrain *terrain = region->get_terrain();
		if (terrain == nullptr) {
			continue;
		}

		const float block_size = terrain->get_mesh_block_size() * region_size_blocks;
		const Vector3i block_position = region->get_block_position();
		Ref<NavigationMesh> navigation_mesh = region->get_navigation_mesh();
		if (navigation_mesh.is_null() || navigation_mesh->get_polygon_count() == 0) {
			continue;
		}

		for (int axis = 0; axis < Vector3iUtil::AXIS_COUNT; ++axis) {
			if (axis == Vector3::AXIS_Y) {
				continue;
			}
			for (int side = -1; side <= 1; side += 2) {
				Vector3i neighbor_block_position = block_position;
				neighbor_block_position[axis] += side * region_size_blocks;
				VoxelNavRegion3D *neighbor = find_region(*terrain, neighbor_block_position);
				if (neighbor == nullptr || !neighbor->is_inside_tree()) {
					continue;
				}
				bool neighbor_is_in_baked_set = false;
				for (VoxelNavRegion3D *baked_region : regions) {
					if (baked_region == neighbor) {
						neighbor_is_in_baked_set = true;
						break;
					}
				}
				if (neighbor_is_in_baked_set && std::less<VoxelNavRegion3D *>()(neighbor, region)) {
					continue;
				}

				Ref<NavigationMesh> neighbor_navigation_mesh = neighbor->get_navigation_mesh();
				if (neighbor_navigation_mesh.is_null() || neighbor_navigation_mesh->get_polygon_count() == 0) {
					continue;
				}

				if (stitch_navigation_mesh_pair(
							navigation_mesh,
							neighbor_navigation_mesh,
							axis,
							block_size,
							side > 0
					)) {
					region->synchronize_navigation_mesh();
					neighbor->synchronize_navigation_mesh();
					++stitched_pair_count;
				}
			}
		}
	}

	if (stitched_pair_count > 0) {
		zylann::print_line(format(
				"VoxelNavManager3D: stitched shared vertices across {} navigation region edge pair(s)",
				stitched_pair_count
		));
	}
}

void VoxelNavManager3D::clear_regions() {
	++_region_rebuild_id;
	_region_rebuild_in_progress = false;
	_bake_requested_after_region_rebuild = false;
	_pending_region_probe_task_count = 0;
	_region_probe_task_count = 0;
	_region_probe_checked_count = 0;
	_region_probe_skipped_empty_count = 0;
	_region_probe_failed_create_count = 0;
	_region_probe_generate_time_msec = 0;
	_region_probe_scan_time_msec = 0;
	_region_probe_apply_time_msec = 0;
	_region_probe_max_task_time_msec = 0;
	_region_probe_slow_task_count = 0;
	++_nav_source_generation_id;
	_nav_source_generation_in_progress = false;
	_pending_nav_source_task_count = 0;
	_nav_source_task_count = 0;
	_nav_source_empty_count = 0;
	_nav_source_meshed_block_count = 0;
	_nav_source_empty_block_count = 0;
	_nav_source_worker_time_msec = 0;
	_nav_source_apply_time_msec = 0;
	_nav_source_max_task_time_msec = 0;
	++_nav_bake_generation_id;
	_nav_bake_in_progress = false;
	_pending_nav_bake_task_count = 0;
	_nav_bake_task_count = 0;
	_nav_bake_baked_count = 0;
	_nav_bake_polygon_count = 0;
	_nav_bake_removed_empty_region_count = 0;
	_nav_bake_total_time_msec = 0;
	_nav_bake_max_time_msec = 0;
	_nav_bake_apply_total_time_msec = 0;
	_nav_bake_apply_max_time_msec = 0;
	_nav_bake_finalize_time_msec = 0;
	_nav_bake_cleanup_time_msec = 0;
	_nav_bake_stitch_time_msec = 0;
	_nav_bake_wall_time_before_msec = 0;
	_pending_bake_regions.clear();
	clear_regions_internal();
}

void VoxelNavManager3D::clear_regions_internal() {
	_region_map.clear();
	for (VoxelNavRegion3D *region : _regions) {
		if (region == nullptr) {
			continue;
		}
		if (region->is_inside_tree()) {
			region->clear_navigation_mesh();
		}
		if (region->get_parent() != this) {
			continue;
		}
		remove_child(region);
		memdelete(region);
	}
	_regions.clear();

	for (int i = get_child_count() - 1; i >= 0; --i) {
		VoxelNavRegion3D *region = Object::cast_to<VoxelNavRegion3D>(get_child(i));
		if (region == nullptr) {
			continue;
		}
		if (!region->has_meta(GENERATED_REGION_META_NAME) && !String(region->get_name()).begins_with("VoxelNavRegion_")) {
			continue;
		}
		region->clear_navigation_mesh();
		remove_child(region);
		memdelete(region);
	}
}

int VoxelNavManager3D::get_region_count() const {
	return _regions.size();
}

void VoxelNavManager3D::collect_voxel_lod_terrains(Node *node, StdVector<VoxelLodTerrain *> &out_terrains) const {
	for (int i = 0; i < node->get_child_count(); ++i) {
		Node *child = node->get_child(i);
		VoxelLodTerrain *terrain = Object::cast_to<VoxelLodTerrain>(child);
		if (terrain != nullptr) {
			out_terrains.push_back(terrain);
			continue;
		}
		if (Object::cast_to<VoxelNavRegion3D>(child) == nullptr) {
			collect_voxel_lod_terrains(child, out_terrains);
		}
	}
}

VoxelNavRegion3D *VoxelNavManager3D::find_region(VoxelLodTerrain &terrain, Vector3i block_position) const {
	const auto it = _region_map.find({ &terrain, block_position });
	if (it == _region_map.end()) {
		return nullptr;
	}
	return it->second;
}

void VoxelNavManager3D::unregister_region(VoxelNavRegion3D &region) {
	VoxelLodTerrain *terrain = region.get_terrain();
	if (terrain == nullptr) {
		return;
	}
	_region_map.erase({ terrain, region.get_block_position() });
}

VoxelNavRegion3D *VoxelNavManager3D::create_region(VoxelLodTerrain &terrain, Vector3i block_position) {
	VoxelNavRegion3D *region = memnew(VoxelNavRegion3D);
	region->set_name(
			String("VoxelNavRegion_{0}_{1}_{2}").format(varray(block_position.x, block_position.y, block_position.z))
	);
	add_child(region);
	if (get_owner() != nullptr) {
		region->set_owner(get_owner());
	}
	region->set_meta(GENERATED_REGION_META_NAME, true);
	region->set_navigation_layers(_navigation_layers);

	const float block_size = terrain.get_mesh_block_size();
	const Transform3D block_transform(Basis(), to_vec3(block_position) * block_size);
	const Transform3D region_transform =
			get_global_transform().affine_inverse() * terrain.get_global_transform() * block_transform;
	if (!region->setup_from_voxel_block(
				&terrain, block_position, _region_size_power, _nav_mesh_settings, region_transform
		)) {
		remove_child(region);
		memdelete(region);
		return nullptr;
	}
	_region_map[{ &terrain, block_position }] = region;
	return region;
}

void VoxelNavManager3D::refresh_terrain_connections() {
	disconnect_terrains();

	StdVector<VoxelLodTerrain *> terrains;
	collect_voxel_lod_terrains(this, terrains);
	for (VoxelLodTerrain *terrain : terrains) {
		if (terrain != nullptr) {
			connect_terrain(*terrain);
		}
	}
}

void VoxelNavManager3D::connect_terrain(VoxelLodTerrain &terrain) {
	_connected_terrains.push_back(&terrain);
	terrain.connect(
			VoxelStringNames::get_singleton().voxel_area_edited,
			callable_mp(this, &VoxelNavManager3D::_on_terrain_voxel_area_edited)
	);
}

void VoxelNavManager3D::disconnect_terrains() {
	const Callable callable = callable_mp(this, &VoxelNavManager3D::_on_terrain_voxel_area_edited);
	for (VoxelLodTerrain *terrain : _connected_terrains) {
		if (terrain == nullptr) {
			continue;
		}
		if (terrain->is_connected(VoxelStringNames::get_singleton().voxel_area_edited, callable)) {
			terrain->disconnect(VoxelStringNames::get_singleton().voxel_area_edited, callable);
		}
	}
	_connected_terrains.clear();
}

void VoxelNavManager3D::_on_terrain_voxel_area_edited(VoxelLodTerrain *terrain, Vector3i position, Vector3i size) {
	if (terrain == nullptr) {
		return;
	}
	update_regions_for_terrain_area(*terrain, Box3i(position, size));
}

void VoxelNavManager3D::update_regions_for_terrain_area(VoxelLodTerrain &terrain, Box3i voxel_box) {
	if (voxel_box.is_empty()) {
		return;
	}
	if (_region_rebuild_in_progress) {
		_bake_requested_after_region_rebuild = true;
		return;
	}

	const int mesh_block_size = terrain.get_mesh_block_size();
	const int region_size_blocks = 1 << _region_size_power;
	const int region_size_voxels = mesh_block_size * region_size_blocks;
	const Box3i affected_region_bounds = voxel_box.padded(mesh_block_size).downscaled(region_size_voxels);
	const uint64_t region_count = Vector3iUtil::get_volume_u64(affected_region_bounds.size);

	if (region_count > MAX_REGIONS_TO_CREATE) {
		ZN_PRINT_WARNING(format(
				"VoxelNavManager3D: skipped incremental nav update because edited area would touch {} regions",
				region_count
		));
		return;
	}

	StdVector<ManagedRegionKey> region_keys;
	region_keys.reserve(region_count);
	affected_region_bounds.for_each_cell_zxy([&](const Vector3i region_position) {
		region_keys.push_back({ &terrain, region_position * region_size_blocks });
	});

	zylann::print_line(format(
			"VoxelNavManager3D: terrain edit pos=({}, {}, {}) size=({}, {}, {}) affects {} navigation region(s)",
			voxel_box.position.x,
			voxel_box.position.y,
			voxel_box.position.z,
			voxel_box.size.x,
			voxel_box.size.y,
			voxel_box.size.z,
			region_keys.size()
	));
	schedule_navigation_source_mesh_tasks_for_regions(to_span_const(region_keys));
}

void VoxelNavManager3D::_notification(int what) {
	switch (what) {
		case NOTIFICATION_ENTER_TREE:
			refresh_terrain_connections();
			break;

		case NOTIFICATION_READY:
			refresh_terrain_connections();
			if (_bake_on_ready) {
				rebuild_regions();
				bake_navigation_meshes();
			}
			break;

		case NOTIFICATION_EXIT_TREE:
			disconnect_terrains();
			break;
	}
}

void VoxelNavManager3D::_bind_methods() {
	using Self = VoxelNavManager3D;

	ClassDB::bind_method(D_METHOD("set_nav_mesh_settings", "settings"), &Self::set_nav_mesh_settings);
	ClassDB::bind_method(D_METHOD("get_nav_mesh_settings"), &Self::get_nav_mesh_settings);

	ClassDB::bind_method(D_METHOD("set_bake_on_ready", "enabled"), &Self::set_bake_on_ready);
	ClassDB::bind_method(D_METHOD("get_bake_on_ready"), &Self::get_bake_on_ready);

	ClassDB::bind_method(D_METHOD("set_navigation_layers", "navigation_layers"), &Self::set_navigation_layers);
	ClassDB::bind_method(D_METHOD("get_navigation_layers"), &Self::get_navigation_layers);
	ClassDB::bind_method(D_METHOD("set_navigation_layer_value", "layer_number", "value"), &Self::set_navigation_layer_value);
	ClassDB::bind_method(D_METHOD("get_navigation_layer_value", "layer_number"), &Self::get_navigation_layer_value);

	ClassDB::bind_method(D_METHOD("set_region_size_power", "power"), &Self::set_region_size_power);
	ClassDB::bind_method(D_METHOD("get_region_size_power"), &Self::get_region_size_power);
	ClassDB::bind_method(D_METHOD("set_fine_occupancy_enabled", "enabled"), &Self::set_fine_occupancy_enabled);
	ClassDB::bind_method(D_METHOD("is_fine_occupancy_enabled"), &Self::is_fine_occupancy_enabled);

	ClassDB::bind_method(D_METHOD("rebuild_regions"), &Self::rebuild_regions);
	ClassDB::bind_method(D_METHOD("bake_navigation_meshes"), &Self::bake_navigation_meshes);
	ClassDB::bind_method(D_METHOD("clear_regions"), &Self::clear_regions);
	ClassDB::bind_method(D_METHOD("get_region_count"), &Self::get_region_count);

	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "nav_mesh_settings", PROPERTY_HINT_RESOURCE_TYPE, "VoxelNavMeshSettings"),
			"set_nav_mesh_settings",
			"get_nav_mesh_settings"
	);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bake_on_ready"), "set_bake_on_ready", "get_bake_on_ready");
	ADD_PROPERTY(
			PropertyInfo(Variant::INT, "navigation_layers", PROPERTY_HINT_LAYERS_3D_NAVIGATION),
			"set_navigation_layers",
			"get_navigation_layers"
	);
	ADD_PROPERTY(
			PropertyInfo(Variant::INT, "region_size_power", PROPERTY_HINT_RANGE, "0,8,1"),
			"set_region_size_power",
			"get_region_size_power"
	);
	ADD_PROPERTY(
			PropertyInfo(Variant::BOOL, "fine_occupancy_enabled"),
			"set_fine_occupancy_enabled",
			"is_fine_occupancy_enabled"
	);
}

} // namespace zylann::voxel
