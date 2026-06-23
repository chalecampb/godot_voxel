#include "voxel_sdf_material_library.h"
#include "../../util/godot/classes/object.h"
#include "../../util/godot/core/class_db.h"

namespace zylann::voxel {

void VoxelSdfMaterialLibrary::set_materials(TypedArray<Material> materials) {
	_materials.resize(materials.size());
	for (int i = 0; i < materials.size(); ++i) {
		_materials[i] = materials[i];
	}
	emit_changed();
}

TypedArray<Material> VoxelSdfMaterialLibrary::get_materials() const {
	TypedArray<Material> materials;
	materials.resize(_materials.size());
	for (unsigned int i = 0; i < _materials.size(); ++i) {
		materials[i] = _materials[i];
	}
	return materials;
}

Ref<Material> VoxelSdfMaterialLibrary::get_material_by_index(unsigned int index) const {
	if (index >= _materials.size()) {
		return Ref<Material>();
	}
	return _materials[index];
}

unsigned int VoxelSdfMaterialLibrary::get_material_index_count() const {
	return _materials.size();
}

Ref<Material> VoxelSdfMaterialLibrary::_b_get_material_by_index(int index) const {
	if (index < 0) {
		return Ref<Material>();
	}
	return get_material_by_index(index);
}

int VoxelSdfMaterialLibrary::_b_get_material_index_count() const {
	return _materials.size();
}

void VoxelSdfMaterialLibrary::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_materials", "materials"), &VoxelSdfMaterialLibrary::set_materials);
	ClassDB::bind_method(D_METHOD("get_materials"), &VoxelSdfMaterialLibrary::get_materials);
	ClassDB::bind_method(D_METHOD("get_material_by_index", "index"), &VoxelSdfMaterialLibrary::_b_get_material_by_index);
	ClassDB::bind_method(D_METHOD("get_material_index_count"), &VoxelSdfMaterialLibrary::_b_get_material_index_count);

	ADD_PROPERTY(
			PropertyInfo(
					Variant::ARRAY,
					"materials",
					PROPERTY_HINT_ARRAY_TYPE,
					MAKE_RESOURCE_TYPE_HINT("Material")
			),
			"set_materials",
			"get_materials"
	);
}

} // namespace zylann::voxel
