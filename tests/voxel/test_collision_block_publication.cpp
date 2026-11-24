#include "test_collision_block_publication.h"
#include "../../engine/voxel_engine.h"
#include "../../meshers/mesh_block_task.h"
#include "../../terrain/variable_lod/collision_block_publication.h"
#include "../../terrain/variable_lod/collision_segment_coverage.h"
#include "../../util/testing/test_macros.h"

namespace zylann::voxel::tests {

using Publication = CollisionBlockPublication;
using PublicationState = Publication::State;

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_block_publication_default_and_queued_work() {
	Publication publication;
	ZN_TEST_ASSERT(publication.get_ticket() == 0);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::UNAVAILABLE);

	publication.begin_request(1);
	ZN_TEST_ASSERT(publication.get_ticket() == 1);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::UNAVAILABLE);
}

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_block_publication_rejects_zero_and_wrong_tickets() {
	Publication publication;
	publication.begin_request(7);

	ZN_TEST_ASSERT(publication.publish_empty(0) == false);
	ZN_TEST_ASSERT(publication.publish_body(0) == false);
	ZN_TEST_ASSERT(publication.publish_empty(8) == false);
	ZN_TEST_ASSERT(publication.publish_body(8) == false);
	ZN_TEST_ASSERT(publication.get_ticket() == 7);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::UNAVAILABLE);

	publication.begin_request(0);
	ZN_TEST_ASSERT(publication.get_ticket() == 0);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::UNAVAILABLE);
}

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_block_publication_exact_empty_and_duplicate() {
	Publication publication;
	publication.begin_request(11);
	ZN_TEST_ASSERT(publication.publish_empty(11));
	ZN_TEST_ASSERT(publication.get_ticket() == 11);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::EMPTY);

	ZN_TEST_ASSERT(publication.publish_empty(11) == false);
	ZN_TEST_ASSERT(publication.publish_body(11) == false);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::EMPTY);
}

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_block_publication_exact_body_after_completion() {
	Publication publication;
	publication.begin_request(13);

	// Mesh completion alone does not publish collision; the owner calls this only after its physics fence.
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::UNAVAILABLE);
	ZN_TEST_ASSERT(publication.publish_body(13));
	ZN_TEST_ASSERT(publication.get_ticket() == 13);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::BODY);
}

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_block_publication_edit_invalidates_late_completion() {
	Publication publication;
	publication.begin_request(17);
	ZN_TEST_ASSERT(publication.publish_body(17));

	publication.begin_request(18);
	ZN_TEST_ASSERT(publication.get_ticket() == 18);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::UNAVAILABLE);
	ZN_TEST_ASSERT(publication.publish_body(17) == false);
	ZN_TEST_ASSERT(publication.publish_empty(17) == false);
	ZN_TEST_ASSERT(publication.get_ticket() == 18);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::UNAVAILABLE);
}

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_block_publication_clear_and_reuse() {
	Publication publication;
	publication.begin_request(23);
	ZN_TEST_ASSERT(publication.publish_body(23));

	publication.clear();
	ZN_TEST_ASSERT(publication.get_ticket() == 0);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::UNAVAILABLE);

	publication.begin_request(24);
	ZN_TEST_ASSERT(publication.get_ticket() == 24);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::UNAVAILABLE);
	ZN_TEST_ASSERT(publication.publish_empty(24));
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::EMPTY);
}

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_block_publication_preserves_unaffected_block() {
	Publication edited_block;
	Publication unaffected_block;
	edited_block.begin_request(31);
	unaffected_block.begin_request(41);
	ZN_TEST_ASSERT(edited_block.publish_body(31));
	ZN_TEST_ASSERT(unaffected_block.publish_empty(41));

	edited_block.begin_request(32);
	ZN_TEST_ASSERT(edited_block.get_state() == PublicationState::UNAVAILABLE);
	ZN_TEST_ASSERT(unaffected_block.get_ticket() == 41);
	ZN_TEST_ASSERT(unaffected_block.get_state() == PublicationState::EMPTY);
}

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_request_tickets_are_positive_and_per_volume() {
	CollisionRequestTickets first_volume;
	CollisionRequestTickets second_volume;

	const uint64_t first_ticket = first_volume.issue();
	const uint64_t second_ticket = first_volume.issue();
	const uint64_t independent_ticket = second_volume.issue();

	ZN_TEST_ASSERT(first_ticket == 1);
	ZN_TEST_ASSERT(second_ticket == 2);
	ZN_TEST_ASSERT(independent_ticket == 1);
	ZN_TEST_ASSERT(first_ticket != 0);
	ZN_TEST_ASSERT(second_ticket != 0);
	ZN_TEST_ASSERT(independent_ticket != 0);
}

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_request_ticket_sequence_survives_publication_clear() {
	CollisionRequestTickets tickets;
	CollisionBlockPublication publication;

	const uint64_t first_ticket = tickets.issue();
	publication.begin_request(first_ticket);
	publication.clear();
	const uint64_t second_ticket = tickets.issue();

	ZN_TEST_ASSERT(publication.get_ticket() == 0);
	ZN_TEST_ASSERT(first_ticket == 1);
	ZN_TEST_ASSERT(second_ticket == 2);
}

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_request_ticket_defaults_on_task_and_output() {
	MeshBlockTask task;
	VoxelEngine::BlockMeshOutput output;

	ZN_TEST_ASSERT(task.collision_request_ticket == 0);
	ZN_TEST_ASSERT(output.collision_request_ticket == 0);
}

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_block_result_selection_preserves_current_over_stale_arrival() {
	// A late stale or visual-only result must not displace an already queued current collision result.
	ZN_TEST_ASSERT(CollisionBlockPublication::is_current_result(41, 41));
	ZN_TEST_ASSERT(CollisionBlockPublication::is_current_result(0, 41) == false);
	ZN_TEST_ASSERT(CollisionBlockPublication::is_current_result(40, 41) == false);
	ZN_TEST_ASSERT(CollisionBlockPublication::is_current_result(41, 42) == false);
}

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_segment_coverage_closed_intersections() {
	using Coverage = CollisionSegmentCoverage;
	ZN_TEST_ASSERT(Coverage::segment_intersects_closed_box(
			Vector3(-1, 0.5, 0.5), Vector3(2, 0.5, 0.5), Vector3(0, 0, 0), Vector3(1, 1, 1)));
	ZN_TEST_ASSERT(Coverage::segment_intersects_closed_box(
			Vector3(1, 1, -1), Vector3(1, 1, 2), Vector3(0, 0, 0), Vector3(1, 1, 1)));
	ZN_TEST_ASSERT(Coverage::segment_intersects_closed_box(
			Vector3(-1, -1, -1), Vector3(0, 0, 0), Vector3(0, 0, 0), Vector3(1, 1, 1)));
	ZN_TEST_ASSERT(Coverage::segment_intersects_closed_box(
			Vector3(0.5, 0.5, 0.5), Vector3(0.5, 0.5, 0.5), Vector3(0, 0, 0), Vector3(1, 1, 1)));
	ZN_TEST_ASSERT(Coverage::segment_intersects_closed_box(
			Vector3(0, 0, 0), Vector3(0, 0, 0), Vector3(0, 0, 0), Vector3(1, 1, 1)));
}

// Requirements: WRLD-10, WRLD-17, WRLD-21
void test_collision_segment_coverage_rejects_invalid_and_over_budget_inputs() {
	using Coverage = CollisionSegmentCoverage;
	ZN_TEST_ASSERT(Coverage::is_valid_query(Vector3(0, 0, 0), Vector3(1, 1, 1), 1));
	ZN_TEST_ASSERT(Coverage::is_valid_query(Vector3(0, 0, 0), Vector3(1, 1, 1), 32));
	ZN_TEST_ASSERT(Coverage::is_valid_query(Vector3(0, 0, 0), Vector3(1, 1, 1), 0) == false);
	ZN_TEST_ASSERT(Coverage::is_valid_query(Vector3(0, 0, 0), Vector3(1, 1, 1), 33) == false);
	ZN_TEST_ASSERT(Coverage::is_valid_query(Vector3(INFINITY, 0, 0), Vector3(1, 1, 1), 1) == false);
}

} // namespace zylann::voxel::tests
