#ifndef VOXEL_NAV_MANAGER_3D_H
#define VOXEL_NAV_MANAGER_3D_H

#include "../util/containers/std_vector.h"
#include "../util/containers/std_unordered_map.h"
#include "../util/containers/span.h"
#include "../util/math/box3i.h"
#include "voxel_nav_mesh_settings.h"

#include "../util/godot/classes/node_3d.h"
#include <functional>

namespace zylann::voxel {

class VoxelLodTerrain;
class VoxelNavRegion3D;
class VoxelNavOccupancyTask;
class VoxelNavSourceMeshTask;
class VoxelNavBakeTask;

class VoxelNavManager3D : public Node3D {
	GDCLASS(VoxelNavManager3D, Node3D)
public:
	VoxelNavManager3D();

	void set_nav_mesh_settings(Ref<VoxelNavMeshSettings> settings);
	Ref<VoxelNavMeshSettings> get_nav_mesh_settings() const;

	void set_bake_on_ready(bool enabled);
	bool get_bake_on_ready() const;

	void set_navigation_layers(uint32_t navigation_layers);
	uint32_t get_navigation_layers() const;
	void set_navigation_layer_value(int layer_number, bool value);
	bool get_navigation_layer_value(int layer_number) const;

	void set_region_size_power(int power);
	int get_region_size_power() const;

	void set_fine_occupancy_enabled(bool enabled);
	bool is_fine_occupancy_enabled() const;

	void rebuild_regions();
	void bake_navigation_meshes();
	void clear_regions();

	int get_region_count() const;

protected:
	void _notification(int what);

private:
	friend class VoxelNavOccupancyTask;
	friend class VoxelNavSourceMeshTask;
	friend class VoxelNavBakeTask;

	struct ManagedRegionKey {
		VoxelLodTerrain *terrain = nullptr;
		Vector3i block_position;

		inline bool operator==(const ManagedRegionKey &other) const {
			return terrain == other.terrain && block_position == other.block_position;
		}
	};

	struct ManagedRegionKeyHasher {
		size_t operator()(const ManagedRegionKey &key) const {
			size_t hash = std::hash<VoxelLodTerrain *>()(key.terrain);
			hash ^= std::hash<Vector3i>()(key.block_position) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
			return hash;
		}
	};

	static void _bind_methods();

	void collect_voxel_lod_terrains(Node *node, StdVector<VoxelLodTerrain *> &out_terrains) const;
	VoxelNavRegion3D *create_region(VoxelLodTerrain &terrain, Vector3i block_position);
	VoxelNavRegion3D *find_region(VoxelLodTerrain &terrain, Vector3i block_position) const;
	void unregister_region(VoxelNavRegion3D &region);
	void clear_regions_internal();
	void refresh_terrain_connections();
	void connect_terrain(VoxelLodTerrain &terrain);
	void disconnect_terrains();
	void _on_terrain_voxel_area_edited(VoxelLodTerrain *terrain, Vector3i position, Vector3i size);
	void update_regions_for_terrain_area(VoxelLodTerrain &terrain, Box3i voxel_box);
	void schedule_navigation_source_mesh_tasks();
	void schedule_navigation_source_mesh_tasks_for_regions(Span<const ManagedRegionKey> region_keys);
	void bake_prebuilt_navigation_meshes(Span<VoxelNavRegion3D *> regions);
	void finish_navigation_bake_batch();
	void stitch_baked_region_edges(Span<VoxelNavRegion3D *> regions);

	Ref<VoxelNavMeshSettings> _nav_mesh_settings;
	StdVector<VoxelNavRegion3D *> _regions;
	uint32_t _navigation_layers = 1;
	int _region_size_power = 0;
	bool _fine_occupancy_enabled = false;
	bool _bake_on_ready = false;
	bool _region_rebuild_in_progress = false;
	bool _bake_requested_after_region_rebuild = false;
	uint32_t _region_rebuild_id = 0;
	int _pending_region_probe_task_count = 0;
	int _region_probe_task_count = 0;
	int _region_probe_checked_count = 0;
	int _region_probe_skipped_empty_count = 0;
	int _region_probe_failed_create_count = 0;
	uint64_t _region_probe_generate_time_msec = 0;
	uint64_t _region_probe_scan_time_msec = 0;
	uint64_t _region_probe_apply_time_msec = 0;
	uint64_t _region_probe_max_task_time_msec = 0;
	int _region_probe_slow_task_count = 0;
	bool _nav_source_generation_in_progress = false;
	uint32_t _nav_source_generation_id = 0;
	int _pending_nav_source_task_count = 0;
	int _nav_source_task_count = 0;
	int _nav_source_empty_count = 0;
	int _nav_source_meshed_block_count = 0;
	int _nav_source_empty_block_count = 0;
	uint64_t _nav_source_worker_time_msec = 0;
	uint64_t _nav_source_apply_time_msec = 0;
	uint64_t _nav_source_max_task_time_msec = 0;
	bool _nav_bake_in_progress = false;
	uint32_t _nav_bake_generation_id = 0;
	int _pending_nav_bake_task_count = 0;
	int _nav_bake_task_count = 0;
	int _nav_bake_baked_count = 0;
	int _nav_bake_polygon_count = 0;
	int _nav_bake_removed_empty_region_count = 0;
	uint64_t _nav_bake_total_time_msec = 0;
	uint64_t _nav_bake_max_time_msec = 0;
	uint64_t _nav_bake_apply_total_time_msec = 0;
	uint64_t _nav_bake_apply_max_time_msec = 0;
	uint64_t _nav_bake_finalize_time_msec = 0;
	uint64_t _nav_bake_cleanup_time_msec = 0;
	uint64_t _nav_bake_stitch_time_msec = 0;
	uint64_t _nav_bake_wall_time_before_msec = 0;
	StdVector<VoxelLodTerrain *> _connected_terrains;
	StdUnorderedMap<ManagedRegionKey, VoxelNavRegion3D *, ManagedRegionKeyHasher> _region_map;
	StdVector<VoxelNavRegion3D *> _pending_bake_regions;
};

} // namespace zylann::voxel

#endif // VOXEL_NAV_MANAGER_3D_H
