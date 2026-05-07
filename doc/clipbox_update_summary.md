# Clipbox Update Summary

This summarizes the clipbox improvement cherry-picked from `sgn_remake` and the follow-up mesh-priority cleanup on `sgn_clipbox_updates`.

## Hysteresis

| Updated code | Line-by-line summary |
| --- | --- |
| `get_data_box_hysteresis_margin(required_box)` | Adds a helper for choosing how much extra data to keep around the required data clipbox. |
| `longest_axis = max(size.x, max(size.y, size.z))` | Bases the margin on the largest clipbox axis so wider view boxes get a proportionally larger stability buffer. |
| `clamp(longest_axis / 16, 1, 8)` | Uses one sixteenth of the clipbox span, with a hard minimum of 1 block and maximum of 8 blocks. |
| `get_stabilized_data_box(required_box, previous_box, bounds)` | Adds a single helper that owns the data-box hysteresis decision. |
| `if required_box.is_empty()` | Leaves empty boxes unchanged so disabled or fully clipped cases do not grow unexpectedly. |
| `clipped_previous_box = previous_box.clipped(bounds)` | Revalidates the previous data box against current terrain bounds before reusing it. |
| `if clipped_previous_box contains required_box` | Reuses the previous data box when the new required box still fits inside it. |
| `return required_box.padded(margin).clipped(bounds)` | Expands the data box only when the required box escapes the previous stable box, then clips it to terrain bounds. |
| `data_box` renamed to `required_data_box` in mesh-driven mode | Separates the minimum data required for meshes from the stabilized data box actually stored in viewer state. |
| `state.data_box_per_lod[lod] = get_stabilized_data_box(...)` in mesh-driven mode | Applies hysteresis to data boxes derived from mesh clipboxes. |
| `new_data_box` renamed to `required_data_box` in distance-driven mode | Gives the distance-derived data clipbox the same minimum-vs-stabilized naming. |
| `state.data_box_per_lod[lod] = get_stabilized_data_box(...)` in distance-driven mode | Applies the same hysteresis behavior to data boxes derived directly from viewer distance. |

## Large Movement Cost Fix

| Updated code | Line-by-line summary |
| --- | --- |
| `unreference_data_block_from_loading_lists(...)` signature | Removes `data_blocks_to_load` and `lod_index` from the per-block unload cancellation path. |
| `loading_blocks.find(bpos)` | Keeps the fast lookup for the active loading block entry. |
| `if loading_block_it == end` | Leaves already-unreferenced or never-loading blocks alone. |
| `viewers.unref()` | Decrements the loading-block viewer reference count exactly as before. |
| `if viewers.get() == 0` | Cancels only when the block has no remaining viewers. |
| `cancellation_token.cancel()` | Keeps task cancellation attached to the loading block entry. |
| `loading_blocks.erase(...)` | Removes the active loading block entry once there are no viewers. |
| Removed per-block scan of `data_blocks_to_load` | Eliminates the quadratic behavior where every outgoing block scanned the whole pending-load vector. |
| `process_data_blocks_sliding_box()` unload loop | Calls the lighter `unreference_data_block_from_loading_lists(lod.loading_blocks, bpos)` for each outgoing missing block. |
| `if data_blocks_to_load.size() > 0` | Adds one cleanup pass after all viewers and LODs have been processed. |
| `dst_i` / `src_i` compaction loop | Compacts the pending load vector in one linear pass. |
| `if token is valid and cancelled` | Drops pending load requests cancelled by the unload path. |
| `data_blocks_to_load[dst_i] = data_blocks_to_load[src_i]` | Preserves non-cancelled requests without repeatedly erasing from the middle of the vector. |
| `data_blocks_to_load.resize(dst_i)` | Truncates removed cancelled requests after compaction. |

## Mesh Build Priority

| Updated code | Line-by-line summary |
| --- | --- |
| `PriorityDependency::get_lod_priority_band(lod_index)` | Adds a named policy for mapping LOD index to task priority. |
| `TaskPriority::BAND_MAX - min(lod_index, BAND_MAX)` | Gives `LOD 0` the highest LOD priority band, then `LOD 1`, then lower priority for increasing LODs, clamped against underflow. |
| `priority.band1 = get_lod_priority_band(lod_index)` | Makes LOD ordering take precedence over distance while preserving existing task type priority and distance priority bands. |
| `test_task_priority_values()` assertions | Adds regression coverage proving the LOD priority band decreases from LOD 0 to LOD 1 to LOD 2 and clamps at 255. |
| `ThreadedTaskRunner` staged-task admission | Recomputes and sorts task priorities immediately when staged tasks enter the runnable queue, so newly nearby mesh tasks do not wait for the periodic priority refresh before outranking old distant tasks. |
| `test_threaded_task_runner_misc()` priority-order case | Adds regression coverage that enqueued tasks run by `TaskPriority`, not by the order they were staged. |
