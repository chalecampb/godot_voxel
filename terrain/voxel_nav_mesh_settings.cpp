#include "voxel_nav_mesh_settings.h"

#include "../util/godot/core/class_db.h"

namespace zylann::voxel {

VoxelNavMeshSettings::VoxelNavMeshSettings() {
	_navigation_mesh_template.instantiate();
}

void VoxelNavMeshSettings::set_navigation_mesh_template(Ref<NavigationMesh> navigation_mesh) {
	_navigation_mesh_template = navigation_mesh;
}

Ref<NavigationMesh> VoxelNavMeshSettings::get_navigation_mesh_template() const {
	return _navigation_mesh_template;
}

Ref<NavigationMesh> VoxelNavMeshSettings::instantiate_navigation_mesh() const {
	if (_navigation_mesh_template.is_valid()) {
		Ref<Resource> resource = _navigation_mesh_template->duplicate(true);
		return resource;
	}

	Ref<NavigationMesh> navigation_mesh;
	navigation_mesh.instantiate();
	return navigation_mesh;
}

void VoxelNavMeshSettings::_bind_methods() {
	using Self = VoxelNavMeshSettings;

	ClassDB::bind_method(
			D_METHOD("set_navigation_mesh_template", "navigation_mesh"), &Self::set_navigation_mesh_template
	);
	ClassDB::bind_method(D_METHOD("get_navigation_mesh_template"), &Self::get_navigation_mesh_template);

	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "navigation_mesh_template", PROPERTY_HINT_RESOURCE_TYPE, "NavigationMesh"),
			"set_navigation_mesh_template",
			"get_navigation_mesh_template"
	);
}

} // namespace zylann::voxel
