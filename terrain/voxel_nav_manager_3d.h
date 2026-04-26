#ifndef VOXEL_NAV_MANAGER_3D_H
#define VOXEL_NAV_MANAGER_3D_H

#include "../util/containers/std_vector.h"
#include "voxel_nav_mesh_settings.h"

#include "../util/godot/classes/node_3d.h"

namespace zylann::voxel {

class VoxelLodTerrain;
class VoxelNavRegion3D;

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

	void rebuild_regions();
	void bake_navigation_meshes();
	void clear_navigation_meshes();
	void clear_regions();

	int get_region_count() const;

protected:
	void _notification(int what);

private:
	static void _bind_methods();

	void collect_voxel_lod_terrains(Node *node, StdVector<VoxelLodTerrain *> &out_terrains) const;
	VoxelNavRegion3D *create_region(VoxelLodTerrain &terrain, Vector3i block_position);

	Ref<VoxelNavMeshSettings> _nav_mesh_settings;
	StdVector<VoxelNavRegion3D *> _regions;
	uint32_t _navigation_layers = 1;
	int _region_size_power = 0;
	bool _bake_on_ready = false;
};

} // namespace zylann::voxel

#endif // VOXEL_NAV_MANAGER_3D_H
