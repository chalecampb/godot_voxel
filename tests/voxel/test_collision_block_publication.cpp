#include "test_collision_block_publication.h"
#include "../../terrain/variable_lod/collision_block_publication.h"
#include "../../util/testing/test_macros.h"

namespace zylann::voxel::tests {

using Publication = CollisionBlockPublication;
using PublicationState = Publication::State;

void test_collision_block_publication_default_and_queued_work() {
	Publication publication;
	ZN_TEST_ASSERT(publication.get_ticket() == 0);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::UNAVAILABLE);

	publication.begin_request(1);
	ZN_TEST_ASSERT(publication.get_ticket() == 1);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::UNAVAILABLE);
}

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

void test_collision_block_publication_exact_body_after_completion() {
	Publication publication;
	publication.begin_request(13);

	// Mesh completion alone does not publish collision; the owner calls this only after its physics fence.
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::UNAVAILABLE);
	ZN_TEST_ASSERT(publication.publish_body(13));
	ZN_TEST_ASSERT(publication.get_ticket() == 13);
	ZN_TEST_ASSERT(publication.get_state() == PublicationState::BODY);
}

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

} // namespace zylann::voxel::tests
