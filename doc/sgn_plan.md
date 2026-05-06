# ScriptGraphNode Release Plan

This document scopes the ScriptGraphNode (SGN) work for review and release. It separates files that implement SGN directly from changes made elsewhere in Voxel Tools, because non-SGN changes have higher regression risk and should be reviewed individually.

## Current Audit Status

The branch has been cleaned to remove stale SGN naming and unrelated work:

- Removed the old untracked `doc/custom_generator_node.md` draft in favor of this SGN-specific plan.
- Removed the untracked generated `doc_data.gen.cpp` artifact from the working tree.
- Removed a legacy prototype load alias from SGN graph loading code. The branch has not released under that name, so carrying the alias adds compatibility code without a real compatibility target.
- Removed the unrelated `util/math/interval.h` clamp change from the effective branch diff. SGN does not require that change.
- A scan of the effective branch diff found no newly-added stale-code markers, debug-print markers, or old SGN naming.

The local `.codex/AGENTS.md` file is intentionally left alone because it contains workspace instructions and is not SGN code.

## SGN-Specific Files

These files are required for SGN itself. They define the resource contract, graph node type, editor workflow, runtime execution, serialization, and shader generation.

| File | Purpose | Required |
| --- | --- | --- |
| `generators/graph/voxel_graph_script_node.h` | Declares `VoxelGraphScriptNode`, `VoxelGraphScriptNodePort`, and `VoxelGraphScriptNodeParameter`. This is the SGN resource contract used by graph nodes and user GDScript. | Yes |
| `generators/graph/voxel_graph_script_node.cpp` | Implements SGN script attachment, contract reload, input/output/parameter metadata, validation, GDScript execution, GLSL validation, revision tracking, and editor warnings. | Yes |
| `register_types.cpp` | Registers the three SGN resource classes with Godot. Without this, scripts and serialized graph resources cannot use them. | Yes |
| `generators/graph/nodes/misc.h` | Registers the `ScriptGraphNode` graph node type and implements CPU buffer execution through per-thread duplicated SGN resources. | Yes |
| `generators/graph/voxel_graph_function.h` | Adds `NODE_SCRIPT_GRAPH` and refresh methods for dynamic SGN ports. | Yes |
| `generators/graph/voxel_graph_function.cpp` | Handles SGN dynamic port setup, port refresh, connection preservation by port name, serialization of dynamic outputs, deserialization of dynamic ports, graph hash invalidation by SGN revision, and SGN subresource change handling. | Yes |
| `generators/graph/voxel_graph_compiler.cpp` | Allows compiled operations to encode dynamic SGN input/output counts instead of relying on fixed `NodeType` port counts. | Yes |
| `generators/graph/voxel_graph_runtime.cpp` | Reads the dynamic SGN port counts while executing, analyzing ranges, and debugging compiled operations. | Yes |
| `generators/graph/voxel_graph_shader_generator.cpp` | Adds SGN GPU support by reading the GLSL backing file, namespacing globals per node, replacing SGN uniforms with parameter constants, and emitting the SGN GLSL call. | Yes for GPU SGN |
| `generators/graph/code_gen_helper.h` | Stores included shader library names as `StdString` instead of raw `const char *`, so dynamically-generated SGN shader names deduplicate by content rather than pointer address. | Yes for GPU SGN |
| `generators/graph/code_gen_helper.cpp` | Matches the `StdString` library-name storage change. | Yes for GPU SGN |
| `generators/graph/program_graph.cpp` | Makes `can_connect` validate port existence before checking duplicate/cycle state, avoiding invalid access when SGN dynamic ports change. | Yes |
| `generators/graph/voxel_generator_graph.cpp` | Hides the built-in `script` property in the generator graph inspector to reduce confusion with SGN script assignment. | UX-only, low risk |
| `generators/graph/voxel_generator_graph.h` | Declares the inspector property validation hook. | UX-only, low risk |
| `editor/graph/voxel_graph_node_inspector_wrapper.cpp` | Adds SGN inspector fields for GDScript, GLSL path, validation messages, and exported parameters; writes SGN values through undo/redo; refreshes graph ports after edits. | Yes for editor workflow |
| `editor/graph/voxel_graph_node_inspector_wrapper.h` | Declares the inspector property validation hook used to hide the wrapper's built-in `script` property for SGN nodes. | Yes for editor workflow |
| `editor/graph/voxel_graph_editor.cpp` | Refreshes SGN node layouts when scripts/resources change, preserves output connections on layout rebuild, avoids preview recursion during graph changes, and refreshes SGN nodes from changed paths. | Yes for editor workflow |
| `editor/graph/voxel_graph_editor.h` | Declares SGN refresh entrypoint and preview recursion guard state. | Yes for editor workflow |
| `editor/graph/voxel_graph_editor_node.cpp` | Adds SGN subtitle display and title refresh support. | UX-only, low risk |
| `editor/graph/voxel_graph_editor_node.h` | Stores the SGN subtitle label. | UX-only, low risk |
| `editor/graph/voxel_graph_editor_plugin.cpp` | Listens to editor resource reload/source-change signals, refreshes SGN metadata after GDScript/GLSL changes, and compiles graph/shaders before editor regeneration. | Yes for editor workflow |
| `editor/graph/voxel_graph_editor_plugin.h` | Declares the SGN reload handlers. | Yes for editor workflow |
| `util/godot/classes/editor_file_system.h` | Adds the Godot/GDExtension wrapper needed by the graph editor plugin to listen for resource reloads. | Yes for editor workflow |
| `util/godot/classes/resource_loader.h` | Declares resource reload with cache replacement. | Yes |
| `util/godot/classes/resource_loader.cpp` | Implements cache-replacing reload so edited SGN GDScript resources can be re-read by the editor. | Yes |

## Voxel Tools Changes Required By SGN

These edits are outside the SGN graph/editor files. Each should be reviewed carefully because it touches existing terrain, task, or GPU behavior.

| File | Change | Required For SGN | Why It Is Needed | Risk |
| --- | --- | --- | --- | --- |
| `editor/terrain/voxel_terrain_editor_plugin.cpp` | Compile `VoxelGeneratorGraph` and GPU shaders before `restart_stream()` from the terrain editor menu. | Yes | SGN scripts and GLSL can change without the terrain node property changing. A manual regenerate must compile the latest graph contract and shader source before new stream tasks are sent. | Low. Limited to editor menu regeneration and graph generators. |
| `engine/gpu/gpu_task_runner.cpp` | Track whether local `RenderingDevice` creation was attempted, reset flags on start, clear availability on shutdown. | Yes for clean GPU UX | SGN GPU compatibility checks and warnings should not report renderer failure before the GPU runner has actually tried to create a device. | Low. State is atomic and read-only outside runner lifecycle. |
| `engine/gpu/gpu_task_runner.h` | Adds atomic state and accessor for creation-attempt status. | Yes for clean GPU UX | Exposes the runner state needed by terrain warnings. | Low. |
| `engine/voxel_engine.h` | Forwards `was_rendering_device_creation_attempted()`. | Yes for clean GPU UX | Terrain code only sees `VoxelEngine`, not the GPU runner directly. | Low. |
| `terrain/variable_lod/voxel_lod_terrain.cpp` | Invalidate streaming dependency in `stop_streamer()` and delay GPU renderer warning until device creation was attempted. | Yes | SGN reload/regeneration restarts can leave old async stream/generator responses in flight. Dependency reset prevents old responses from being accepted after restart. The warning gate prevents premature SGN GPU warnings. | Medium. `stop_streamer()` is central VLT lifecycle code; behavior is conservative because it invalidates only pending old work. |
| `generators/generate_block_task.cpp` | Stop processing when GPU generation failed; check cancellation before applying generated data. | Yes | SGN GPU shader compilation/runtime failure must not publish stale or incomplete generated block data. Cancellation check prevents old restart-era tasks from applying. | Medium. Affects generated block completion, but only adds early exits for failed/cancelled work. |
| `generators/generate_block_task.h` | Adds GPU failure flag and notification method. | Yes | Needed by GPU task failure path. | Low. |
| `generators/generate_block_gpu_task.cpp` | Treat invalid generator shader RID as a recoverable GPU task failure, mark successful preparation explicitly, and guard RID frees. | Yes | SGN GLSL can be invalid while the user edits it. The engine must fail the task cleanly rather than asserting or freeing invalid GPU resources. | Medium. Touches GPU generation path but only hardens failure handling. |
| `generators/generate_block_gpu_task.h` | Adds `notify_gpu_generation_failed()` to the consumer interface and `_prepared` state. | Yes | Allows GPU tasks to return failure to CPU tasks without pretending results exist. | Low. |
| `meshers/mesh_block_task.cpp` | Early-out invalid meshing dependencies, handle GPU generation failure, fall back to `data->get_generator()` if dependency generator is null, and check cancellation before applying mesh. | Yes | SGN graph edits/restarts can invalidate dependencies while mesh tasks are queued. GPU failures from SGN GLSL must not apply meshes. Generator fallback preserves existing data-driven generation when dependency generator is not populated. | Medium. Mesh task behavior is broad, but changes are defensive early exits/fallbacks. |
| `meshers/mesh_block_task.h` | Adds GPU failure flag and notification method. | Yes | Required by GPU task failure path. | Low. |
| `streams/load_block_data_task.cpp` | Check cancellation before applying loaded stream data. | Yes | Prevents stale load results from older SGN regeneration cycles from being accepted. | Low to medium. Only cancelled tasks are dropped. |
| `generators/multipass/generate_block_multipass_cb_task.cpp` | Check cancellation before applying multipass generated block data. | Yes for consistency | Uses the same stale-result guard as single-pass generation. SGN can be used in graph generation paths that interact with restarts and dependency invalidation. | Low to medium. Only cancelled tasks are dropped. |
| `terrain/variable_lod/voxel_lod_terrain_update_task.h` | Create and attach cancellation tokens when scheduling mesh updates. | Yes | Later cancellation checks are only meaningful if scheduled mesh updates carry a token. | Medium. Update scheduling is central VLT code, but the token is passive unless cancelled. |
| `terrain/variable_lod/voxel_lod_terrain_update_clipbox_streaming.cpp` | Create and attach cancellation tokens when re-queuing parent LOD mesh updates. | Yes | Keeps re-queued mesh updates covered by the same stale-task cancellation mechanism. | Medium. Limited to requeue path. |

## Voxel Tools Changes Not Required By SGN

No unrelated tracked code changes remain in the effective branch diff after cleanup. The previous `util/math/interval.h` edit was unrelated to SGN and has been removed from the effective diff.

The untracked `.codex/AGENTS.md` workspace instruction file remains outside the SGN release scope.

## Minimality Notes

The SGN implementation still has a large footprint because dynamic graph ports cross several existing graph layers: node type registration, graph serialization, graph compilation, runtime execution, shader generation, editor node layout, inspector editing, and resource reload handling. Those changes are coupled by current graph architecture and are not easily reducible without removing either CPU SGN, GPU SGN, or editor live-reload support.

The highest-scrutiny Voxel Tools edits are the async task cancellation and GPU failure handling changes. They are included because SGN introduces a much more common edit/reload/regenerate workflow where old generation tasks can outlive the graph/script/shader version that created them. The edits are deliberately defensive: they invalidate old dependencies, propagate explicit GPU failure, and drop cancelled results before applying them.

## Release Checklist

- Confirm a graph containing SGN loads with dynamic input/output names preserved.
- Confirm changing SGN GDScript updates editor ports and preserves same-named connections.
- Confirm changing SGN GLSL refreshes validation state and GPU shader compilation.
- Confirm CPU generation succeeds for a valid `@tool` SGN script.
- Confirm GPU generation succeeds when GLSL entry point, ports, and uniforms match SGN metadata.
- Confirm invalid GLSL reports a validation/shader error without applying stale block data.
- Confirm terrain editor `Re-generate` compiles the latest graph/shader before restarting stream.
- Confirm VLT restart or SGN reload does not accept old cancelled stream, generator, or mesh task results.
- Run the normal build/test matrix before release. A full local build was not run during this cleanup because the workspace instruction says not to build unless requested.
