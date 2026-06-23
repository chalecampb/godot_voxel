#ifndef VOXEL_SDF_MATERIAL_LIBRARY_H
#define VOXEL_SDF_MATERIAL_LIBRARY_H

#include "../../util/containers/std_vector.h"
#include "../../util/godot/classes/material.h"
#include "../../util/godot/classes/resource.h"
#include "../../util/godot/core/typed_array.h"

namespace zylann::voxel {

class VoxelSdfMaterialLibrary : public Resource {
	GDCLASS(VoxelSdfMaterialLibrary, Resource)

public:
	void set_materials(TypedArray<Material> materials);
	TypedArray<Material> get_materials() const;

	Ref<Material> get_material_by_index(unsigned int index) const;
	unsigned int get_material_index_count() const;

private:
	static void _bind_methods();

	Ref<Material> _b_get_material_by_index(int index) const;
	int _b_get_material_index_count() const;

	StdVector<Ref<Material>> _materials;
};

} // namespace zylann::voxel

#endif // VOXEL_SDF_MATERIAL_LIBRARY_H
