#ifndef VOXEL_TESTS_COLLISION_BLOCK_PUBLICATION_H
#define VOXEL_TESTS_COLLISION_BLOCK_PUBLICATION_H

namespace zylann::voxel::tests {

void test_collision_block_publication_default_and_queued_work();
void test_collision_block_publication_rejects_zero_and_wrong_tickets();
void test_collision_block_publication_exact_empty_and_duplicate();
void test_collision_block_publication_exact_body_after_completion();
void test_collision_block_publication_edit_invalidates_late_completion();
void test_collision_block_publication_clear_and_reuse();
void test_collision_block_publication_preserves_unaffected_block();

} // namespace zylann::voxel::tests

#endif // VOXEL_TESTS_COLLISION_BLOCK_PUBLICATION_H
