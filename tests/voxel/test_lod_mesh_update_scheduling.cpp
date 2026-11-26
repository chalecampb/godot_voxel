#include "test_lod_mesh_update_scheduling.h"
#include "../../terrain/variable_lod/voxel_lod_terrain_update_task.h"
#include "../../util/testing/test_macros.h"

namespace zylann::voxel::tests {

using UpdateTask = VoxelLodTerrainUpdateTask;
using UpdatePolicy = UpdateTask::MeshUpdatePolicy;
using MeshBlockState = VoxelLodTerrainUpdateData::MeshBlockState;
using MeshState = VoxelLodTerrainUpdateData::MeshState;
using MeshToUpdate = VoxelLodTerrainUpdateData::MeshToUpdate;

// Requirements: WRLD-21
void test_lod_mesh_update_scheduling_requested_collision_only() {
	MeshBlockState block;
	block.state = VoxelLodTerrainUpdateData::MESH_NEED_UPDATE;
	block.collision_viewers.add();
	block.collision_loaded = true;
	StdVector<MeshToUpdate> pending;
	const Vector3i position(1, 2, 3);

	UpdateTask::schedule_mesh_update(block, position, pending, false, UpdatePolicy::REQUESTED);

	ZN_TEST_ASSERT(block.state.load() == VoxelLodTerrainUpdateData::MESH_UPDATE_NOT_SENT);
	ZN_TEST_ASSERT(block.update_list_index == 0);
	ZN_TEST_ASSERT(pending.size() == 1);
	ZN_TEST_ASSERT(pending[0].position == position);
	ZN_TEST_ASSERT(pending[0].require_visual == false);
}

// Requirements: WRLD-21
void test_lod_mesh_update_scheduling_requested_visual_only() {
	MeshBlockState block;
	block.state = VoxelLodTerrainUpdateData::MESH_NEED_UPDATE;
	block.mesh_viewers.add();
	block.visual_loaded = true;
	StdVector<MeshToUpdate> pending;
	const Vector3i position(4, 5, 6);

	UpdateTask::schedule_mesh_update(block, position, pending, true, UpdatePolicy::REQUESTED);

	ZN_TEST_ASSERT(block.state.load() == VoxelLodTerrainUpdateData::MESH_UPDATE_NOT_SENT);
	ZN_TEST_ASSERT(block.update_list_index == 0);
	ZN_TEST_ASSERT(pending.size() == 1);
	ZN_TEST_ASSERT(pending[0].position == position);
	ZN_TEST_ASSERT(pending[0].require_visual);
}

// Requirements: WRLD-21
void test_lod_mesh_update_scheduling_requested_without_demand_stays_parked() {
	MeshBlockState block;
	block.state = VoxelLodTerrainUpdateData::MESH_NEED_UPDATE;
	block.visual_loaded = true;
	block.collision_loaded = true;
	StdVector<MeshToUpdate> pending;

	UpdateTask::schedule_mesh_update(block, Vector3i(1, 0, 0), pending, true, UpdatePolicy::REQUESTED);

	ZN_TEST_ASSERT(block.state.load() == VoxelLodTerrainUpdateData::MESH_NEED_UPDATE);
	ZN_TEST_ASSERT(block.update_list_index == -1);
	ZN_TEST_ASSERT(pending.empty());
}

// Requirements: WRLD-21
void test_lod_mesh_update_scheduling_active_only_preserves_inactive_demand() {
	MeshBlockState block;
	block.state = VoxelLodTerrainUpdateData::MESH_NEED_UPDATE;
	block.collision_viewers.add();
	block.collision_loaded = true;
	StdVector<MeshToUpdate> pending;

	UpdateTask::schedule_mesh_update(block, Vector3i(2, 0, 0), pending, false, UpdatePolicy::ACTIVE_ONLY);

	ZN_TEST_ASSERT(block.state.load() == VoxelLodTerrainUpdateData::MESH_NEED_UPDATE);
	ZN_TEST_ASSERT(block.update_list_index == -1);
	ZN_TEST_ASSERT(pending.empty());
}

// Requirements: WRLD-21
void test_lod_mesh_update_scheduling_default_active_block() {
	MeshBlockState block;
	block.state = VoxelLodTerrainUpdateData::MESH_NEED_UPDATE;
	block.visual_active = true;
	block.visual_loaded = true;
	StdVector<MeshToUpdate> pending;
	const Vector3i position(3, 0, 0);

	UpdateTask::schedule_mesh_update(block, position, pending, true);

	ZN_TEST_ASSERT(block.state.load() == VoxelLodTerrainUpdateData::MESH_UPDATE_NOT_SENT);
	ZN_TEST_ASSERT(block.update_list_index == 0);
	ZN_TEST_ASSERT(pending.size() == 1);
	ZN_TEST_ASSERT(pending[0].position == position);
	ZN_TEST_ASSERT(pending[0].require_visual);
}

// Requirements: WRLD-21
void test_lod_mesh_update_scheduling_not_sent_is_deduplicated() {
	MeshBlockState block;
	block.state = VoxelLodTerrainUpdateData::MESH_NEED_UPDATE;
	block.collision_viewers.add();
	block.collision_loaded = true;
	StdVector<MeshToUpdate> pending;
	const Vector3i position(4, 0, 0);

	UpdateTask::schedule_mesh_update(block, position, pending, false, UpdatePolicy::REQUESTED);
	const int first_index = block.update_list_index;
	UpdateTask::schedule_mesh_update(block, position, pending, true, UpdatePolicy::REQUESTED);

	ZN_TEST_ASSERT(block.state.load() == VoxelLodTerrainUpdateData::MESH_UPDATE_NOT_SENT);
	ZN_TEST_ASSERT(first_index == 0);
	ZN_TEST_ASSERT(block.update_list_index == first_index);
	ZN_TEST_ASSERT(pending.size() == 1);
	ZN_TEST_ASSERT(pending[0].position == position);
	ZN_TEST_ASSERT(pending[0].require_visual == false);
}

// Requirements: WRLD-21
void test_lod_mesh_update_scheduling_never_realized_stays_parked() {
	MeshBlockState block;
	block.state = VoxelLodTerrainUpdateData::MESH_NEED_UPDATE;
	block.collision_viewers.add();
	StdVector<MeshToUpdate> pending;

	UpdateTask::schedule_mesh_update(block, Vector3i(5, 0, 0), pending, false, UpdatePolicy::REQUESTED);

	ZN_TEST_ASSERT(block.visual_loaded.load() == false);
	ZN_TEST_ASSERT(block.collision_loaded.load() == false);
	ZN_TEST_ASSERT(block.state.load() == VoxelLodTerrainUpdateData::MESH_NEED_UPDATE);
	ZN_TEST_ASSERT(block.update_list_index == -1);
	ZN_TEST_ASSERT(pending.empty());
}

} // namespace zylann::voxel::tests
