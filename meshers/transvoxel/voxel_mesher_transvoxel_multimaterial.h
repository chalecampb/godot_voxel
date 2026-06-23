#ifndef VOXEL_MESHER_TRANSVOXEL_MULTIMATERIAL_H
#define VOXEL_MESHER_TRANSVOXEL_MULTIMATERIAL_H

#include "../../util/thread/rw_lock.h"
#include "../voxel_mesher.h"
#include "transvoxel.h"
#include "voxel_sdf_material_library.h"

namespace zylann::voxel {

class VoxelMesherTransvoxelMultiMaterial : public VoxelMesher {
	GDCLASS(VoxelMesherTransvoxelMultiMaterial, VoxelMesher)

public:
	VoxelMesherTransvoxelMultiMaterial();
	~VoxelMesherTransvoxelMultiMaterial();

	void build(VoxelMesher::Output &output, const VoxelMesher::Input &input) override;

	int get_used_channels_mask() const override;
	bool is_generating_collision_surface() const override;

	void set_library(Ref<VoxelSdfMaterialLibrary> library);
	Ref<VoxelSdfMaterialLibrary> get_library() const;

	void set_textures_ignore_air_voxels(bool enable);
	bool get_textures_ignore_air_voxels() const;

	void set_transitions_enabled(bool enable);
	bool get_transitions_enabled() const;

	void set_edge_clamp_margin(float margin);
	float get_edge_clamp_margin() const;

	void set_uv_scale(float scale);
	float get_uv_scale() const;

	Ref<Material> get_material_by_index(unsigned int index) const override;
	unsigned int get_material_index_count() const override;

#ifdef TOOLS_ENABLED
	void get_configuration_warnings(PackedStringArray &out_warnings) const override;
#endif

protected:
	static void _bind_methods();

private:
	struct Parameters {
		Ref<VoxelSdfMaterialLibrary> library;
		bool textures_ignore_air_voxels = false;
		bool transitions_enabled = true;
		float edge_clamp_margin = 0.02f;
		float uv_scale = 1.0f;
	};

	mutable RWLock _parameters_lock;
	Parameters _parameters;
};

} // namespace zylann::voxel

#endif // VOXEL_MESHER_TRANSVOXEL_MULTIMATERIAL_H
