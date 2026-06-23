#include "test_transvoxel.h"
#include "../../meshers/transvoxel/voxel_mesher_transvoxel_multimaterial.h"
#include "../../meshers/transvoxel/voxel_mesher_transvoxel.h"
#include "../../util/testing/test_macros.h"

namespace zylann::voxel::tests {

void test_transvoxel_issue772() {
	// There was a wrong assertion check on the values of component indices when texturing mode is SINGLE_S4

	VoxelBuffer voxels(VoxelBuffer::ALLOCATOR_DEFAULT);
	voxels.set_channel_depth(VoxelBuffer::CHANNEL_INDICES, VoxelBuffer::DEPTH_8_BIT);
	voxels.create(Vector3iUtil::create(8));
	{
		Vector3i pos;

		const float h = voxels.get_size().y / 2.f + 0.1f;

		for (pos.z = 0; pos.z < voxels.get_size().z; ++pos.z) {
			for (pos.x = 0; pos.x < voxels.get_size().x; ++pos.x) {
				for (pos.y = 0; pos.y < voxels.get_size().y; ++pos.y) {
					const float gy = pos.y;
					const float sd = gy - h;
					voxels.set_voxel_f(sd, pos, VoxelBuffer::CHANNEL_SDF);
					if (sd < 1.f) {
						const uint8_t material_index = (pos.x + pos.y + pos.z) & 0xff;
						voxels.set_voxel(material_index, pos, VoxelBuffer::CHANNEL_INDICES);
					}
				}
			}
		}
	}

	Ref<VoxelMesherTransvoxel> mesher;
	mesher.instantiate();
	mesher->set_texturing_mode(VoxelMesherTransvoxel::TEXTURES_SINGLE_S4);
	VoxelMesher::Output output;
	// Used to crash
	mesher->build(output, VoxelMesher::Input{ voxels, nullptr, Vector3i(), 0, false, false, false });

	ZN_TEST_ASSERT(!VoxelMesher::is_mesh_empty(output.surfaces));
}

void test_transvoxel_multimaterial_uses_visible_materials() {
	VoxelBuffer voxels(VoxelBuffer::ALLOCATOR_DEFAULT);
	voxels.set_channel_depth(VoxelBuffer::CHANNEL_INDICES, VoxelBuffer::DEPTH_8_BIT);
	voxels.create(Vector3iUtil::create(8));

	const float h = 4.1f;
	Vector3i pos;
	for (pos.z = 0; pos.z < voxels.get_size().z; ++pos.z) {
		for (pos.x = 0; pos.x < voxels.get_size().x; ++pos.x) {
			for (pos.y = 0; pos.y < voxels.get_size().y; ++pos.y) {
				const float sd = static_cast<float>(pos.y) - h;
				voxels.set_voxel_f(sd, pos, VoxelBuffer::CHANNEL_SDF);
				if (sd < 0.f) {
					voxels.set_voxel(pos.y < 3 ? 2 : 1, pos, VoxelBuffer::CHANNEL_INDICES);
				}
			}
		}
	}

	Ref<VoxelMesherTransvoxelMultiMaterial> mesher;
	mesher.instantiate();
	mesher->set_textures_ignore_air_voxels(true);

	VoxelMesher::Output output;
	mesher->build(output, VoxelMesher::Input{ voxels, nullptr, Vector3i(), 0, false, false, false });

	ZN_TEST_ASSERT(output.surfaces.size() == 1);
	ZN_TEST_ASSERT(output.surfaces[0].material_index == 1);
}

void test_transvoxel_multimaterial_exposes_deeper_material_after_dig() {
	VoxelBuffer voxels(VoxelBuffer::ALLOCATOR_DEFAULT);
	voxels.set_channel_depth(VoxelBuffer::CHANNEL_INDICES, VoxelBuffer::DEPTH_8_BIT);
	voxels.create(Vector3iUtil::create(8));

	const float h = 2.1f;
	Vector3i pos;
	for (pos.z = 0; pos.z < voxels.get_size().z; ++pos.z) {
		for (pos.x = 0; pos.x < voxels.get_size().x; ++pos.x) {
			for (pos.y = 0; pos.y < voxels.get_size().y; ++pos.y) {
				const float sd = static_cast<float>(pos.y) - h;
				voxels.set_voxel_f(sd, pos, VoxelBuffer::CHANNEL_SDF);
				if (sd < 0.f) {
					voxels.set_voxel(2, pos, VoxelBuffer::CHANNEL_INDICES);
				}
			}
		}
	}

	Ref<VoxelMesherTransvoxelMultiMaterial> mesher;
	mesher.instantiate();
	mesher->set_textures_ignore_air_voxels(true);

	VoxelMesher::Output output;
	mesher->build(output, VoxelMesher::Input{ voxels, nullptr, Vector3i(), 0, false, false, false });

	ZN_TEST_ASSERT(output.surfaces.size() == 1);
	ZN_TEST_ASSERT(output.surfaces[0].material_index == 2);
}

} // namespace zylann::voxel::tests
