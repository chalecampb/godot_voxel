#ifndef VOXEL_NAV_REGION_3D_H
#define VOXEL_NAV_REGION_3D_H

#include "../util/godot/core/packed_arrays.h"
#include "../util/math/vector3i.h"
#include "../util/godot/classes/array_mesh.h"
#include "voxel_nav_mesh_settings.h"

#if defined(ZN_GODOT)
#if __has_include(<scene/3d/navigation/navigation_region_3d.h>)
#include <scene/3d/navigation/navigation_region_3d.h>
#else
#include <scene/3d/navigation_region_3d.h>
#endif
#include <scene/resources/3d/navigation_mesh_source_geometry_data_3d.h>
#include <servers/navigation_3d/navigation_server_3d.h>
#elif defined(ZN_GODOT_EXTENSION)
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
	void bake_navigation_mesh_from_current_source();
	void clear_navigation_mesh();
	bool has_source_mesh() const;
	void set_source_mesh_from_collision_arrays(
			const PackedVector3Array &vertices,
			const PackedInt32Array &indices,
			uint64_t source_mesh_time_msec
	);
	void clear_source_mesh();
	uint64_t get_last_source_mesh_time_msec() const;
	uint64_t get_last_source_geometry_time_msec() const;
	uint64_t get_last_navigation_bake_time_msec() const;

	Vector3i get_block_position() const;
	VoxelLodTerrain *get_terrain() const;

protected:
	void _notification(int what);

private:
	static void _bind_methods();

	bool update_source_from_properties();
	VoxelLodTerrain *resolve_terrain() const;
	Ref<ArrayMesh> create_source_mesh_from_lod0_collision() const;
	void configure_navigation_mesh_bounds(Ref<NavigationMesh> navigation_mesh) const;

	VoxelLodTerrain *_terrain = nullptr;
	NodePath _terrain_path;
	Vector3i _block_position;
	int _region_size_power = 0;
	Ref<VoxelNavMeshSettings> _settings;
	Ref<ArrayMesh> _source_mesh;
	uint64_t _last_source_mesh_time_msec = 0;
	uint64_t _last_source_geometry_time_msec = 0;
	uint64_t _last_navigation_bake_time_msec = 0;
};

} // namespace zylann::voxel

#endif // VOXEL_NAV_REGION_3D_H
