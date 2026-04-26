#ifndef VOXEL_NAV_MESH_SETTINGS_H
#define VOXEL_NAV_MESH_SETTINGS_H

#include "../util/godot/classes/resource.h"

#if defined(ZN_GODOT)
#if __has_include(<scene/resources/3d/navigation_mesh.h>)
#include <scene/resources/3d/navigation_mesh.h>
#else
#include <scene/resources/navigation_mesh.h>
#endif
#elif defined(ZN_GODOT_EXTENSION)
#include <godot_cpp/classes/navigation_mesh.hpp>
using namespace godot;
#endif

namespace zylann::voxel {

class VoxelNavMeshSettings : public Resource {
	GDCLASS(VoxelNavMeshSettings, Resource)
public:
	VoxelNavMeshSettings();

	void set_navigation_mesh_template(Ref<NavigationMesh> navigation_mesh);
	Ref<NavigationMesh> get_navigation_mesh_template() const;

	Ref<NavigationMesh> instantiate_navigation_mesh() const;

private:
	static void _bind_methods();

	Ref<NavigationMesh> _navigation_mesh_template;
};

} // namespace zylann::voxel

#endif // VOXEL_NAV_MESH_SETTINGS_H
