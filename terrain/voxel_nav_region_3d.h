#ifndef VOXEL_NAV_REGION_3D_H
#define VOXEL_NAV_REGION_3D_H

#include "../util/math/vector3i.h"
#include "../util/godot/classes/array_mesh.h"
#include "voxel_nav_mesh_settings.h"

#if defined(ZN_GODOT)
#if __has_include(<scene/3d/navigation/navigation_region_3d.h>)
#include <scene/3d/navigation/navigation_region_3d.h>
#else
#include <scene/3d/navigation_region_3d.h>
#endif
#if __has_include(<scene/3d/mesh_instance_3d.h>)
#include <scene/3d/mesh_instance_3d.h>
#else
#include <scene/3d/mesh_instance_3d.h>
#endif
#include <scene/resources/3d/navigation_mesh_source_geometry_data_3d.h>
#include <servers/navigation_3d/navigation_server_3d.h>
#elif defined(ZN_GODOT_EXTENSION)
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/navigation_region3d.hpp>
#include <godot_cpp/classes/navigation_mesh_source_geometry_data3d.hpp>
#include <godot_cpp/classes/navigation_server3d.hpp>
using namespace godot;
#endif

namespace zylann::voxel {

class VoxelLodTerrain;

class VoxelNavRegion3D : public NavigationRegion3D {
	GDCLASS(VoxelNavRegion3D, NavigationRegion3D)
public:
	VoxelNavRegion3D();

	void set_terrain_path(NodePath path);
	NodePath get_terrain_path() const;

	void set_block_position(Vector3i block_position);
	void set_region_size_power(int power);
	int get_region_size_power() const;

	bool setup_from_voxel_block(
			VoxelLodTerrain *terrain,
			Vector3i block_position,
			int region_size_power,
			Ref<VoxelNavMeshSettings> settings,
			Transform3D region_transform
	);

	void set_nav_mesh_settings(Ref<VoxelNavMeshSettings> settings);
	Ref<VoxelNavMeshSettings> get_nav_mesh_settings() const;

	void bake_navigation_mesh();
	void clear_navigation_mesh();

	Vector3i get_block_position() const;
	VoxelLodTerrain *get_terrain() const;

protected:
	void _notification(int what);

private:
	static void _bind_methods();

	bool update_source_from_properties();
	VoxelLodTerrain *resolve_terrain() const;
	void ensure_source_mesh_instance();
	Ref<ArrayMesh> create_source_mesh_from_voxel_block(int &out_vertex_count, int &out_index_count) const;
	void configure_navigation_mesh_bounds(Ref<NavigationMesh> navigation_mesh) const;

	VoxelLodTerrain *_terrain = nullptr;
	NodePath _terrain_path;
	Vector3i _block_position;
	int _region_size_power = 0;
	Ref<VoxelNavMeshSettings> _settings;
	MeshInstance3D *_source_mesh_instance = nullptr;
};

} // namespace zylann::voxel

#endif // VOXEL_NAV_REGION_3D_H
