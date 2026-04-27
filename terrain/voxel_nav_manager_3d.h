#ifndef VOXEL_NAV_MANAGER_3D_H
#define VOXEL_NAV_MANAGER_3D_H

#include "../util/containers/std_vector.h"
#include "voxel_nav_mesh_settings.h"

#include "../util/godot/classes/node_3d.h"

namespace zylann::voxel {

class VoxelLodTerrain;
class VoxelNavRegion3D;
class VoxelNavOccupancyTask;
class VoxelNavSourceMeshTask;

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
	void clear_navigation_meshes();
	void clear_regions();

	int get_region_count() const;

protected:
	void _notification(int what);

private:
	friend class VoxelNavOccupancyTask;
	friend class VoxelNavSourceMeshTask;

	static void _bind_methods();

	void collect_voxel_lod_terrains(Node *node, StdVector<VoxelLodTerrain *> &out_terrains) const;
	VoxelNavRegion3D *create_region(VoxelLodTerrain &terrain, Vector3i block_position);
	void clear_regions_internal();
	void schedule_navigation_source_mesh_tasks();
	void bake_prebuilt_navigation_meshes();

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
};

} // namespace zylann::voxel

#endif // VOXEL_NAV_MANAGER_3D_H
