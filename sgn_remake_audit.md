# ScriptGraphNode Remake Audit

This audit covers the effective `sgn_remake_noclip` patchset after trimming broad Voxel Tools hardening out of the working tree. The intent is to keep the PR focused on ScriptGraphNode (SGN): the node resource contract, dynamic graph integration, editor workflow, shader generation, serialization, and tests.

The previous reference audit is `sgn_remake_reference_audit.txt`. That document correctly identified the SGN surface area, but it also treated several async task, GPU task, and VoxelLodTerrain reliability changes as part of the SGN release. Those changes are useful candidates for a separate core reliability PR, but they are no longer considered necessary for this SGN patchset.

## Effective Patch Summary

The cleaned `sgn_remake_noclip` branch is intentionally reduced to the SGN resource, graph integration, editor workflow, shader integration, docs, and tests. The table below maps the original commit labels to the effective final changes they now carry after the review cleanup commits.

| Commit label | Scope | Files |
| --- | --- | --- |
| `SGN: add script graph node resource` | Defines and registers the SGN resource contract: script loading, exported parameters, dynamic ports, GLSL path, validation state, GPU compatibility, CPU execution, and release-scope resource classes. | `generators/graph/voxel_graph_script_node.h`, `generators/graph/voxel_graph_script_node.cpp`, `register_types.cpp` |
| `SGN: integrate dynamic graph runtime node` | Adds `ScriptGraphNode` to the generator graph model/runtime: `Misc` category, dynamic port setup and refresh, copy/serialization connection preservation, SGN graph hash invalidation, dynamic compiled op counts, runtime/range/debug count handling, and safe connection checks during port remapping. | `generators/graph/node_type_db.h`, `generators/graph/nodes/misc.h`, `generators/graph/program_graph.cpp`, `generators/graph/voxel_graph_compiler.cpp`, `generators/graph/voxel_graph_function.h`, `generators/graph/voxel_graph_function.cpp`, `generators/graph/voxel_graph_runtime.cpp` |
| `SGN: generate shaders from script graph nodes` | Adds GPU shader generation for SGN: `VoxelGraphScriptNode` owns external GLSL loading, compatibility validation, per-node namespacing, SGN parameter constant substitution, and source/function-name production; the graph shader generator registers returned library code and emits SGN calls; shader-library deduplication uses string content. | `generators/graph/voxel_graph_script_node.h`, `generators/graph/voxel_graph_script_node.cpp`, `generators/graph/voxel_graph_shader_generator.cpp`, `generators/graph/code_gen_helper.h`, `generators/graph/code_gen_helper.cpp` |
| `SGN: refresh editor nodes and script reloads` | Adds editor workflow support: dynamic layout rebuilds, subtitle refresh, inspector script/path/parameter properties, explicit reload hooks for GDScript and GLSL, cache-replacing resource reloads, preview recursion protection, and generator-owned regeneration preparation before terrain restart. | `editor/graph/voxel_graph_editor.h`, `editor/graph/voxel_graph_editor.cpp`, `editor/graph/voxel_graph_editor_node.h`, `editor/graph/voxel_graph_editor_node.cpp`, `editor/graph/voxel_graph_editor_plugin.h`, `editor/graph/voxel_graph_editor_plugin.cpp`, `editor/graph/voxel_graph_node_inspector_wrapper.cpp`, `util/godot/classes/resource_loader.h`, `util/godot/classes/resource_loader.cpp`, `generators/voxel_generator.h`, `generators/voxel_generator.cpp`, `generators/graph/voxel_generator_graph.h`, `generators/graph/voxel_generator_graph.cpp`, `terrain/fixed_lod/voxel_terrain.h`, `terrain/fixed_lod/voxel_terrain.cpp`, `terrain/variable_lod/voxel_lod_terrain.h`, `terrain/variable_lod/voxel_lod_terrain.cpp` |
| `SGN: document and test script graph nodes` | Adds generated/user-facing graph node docs, SGN fixtures, SGN tests, test runner registration, and this audit. | `doc/graph_nodes.xml`, `doc/source/graph_nodes.md`, `editor/graph/graph_nodes_doc_data.h`, `project/tests/sgn_test_node.gd`, `project/tests/sgn_test_node.glsl`, `tests/tests.cpp`, `tests/voxel/test_voxel_graph.h`, `tests/voxel/test_voxel_graph.cpp`, `sgn_remake_audit.md` |

The effective patch changes these areas:

| Area | Files | Why | Required |
| --- | --- | --- | --- |
| SGN resource contract | `generators/graph/voxel_graph_script_node.h`, `generators/graph/voxel_graph_script_node.cpp` | Defines `VoxelGraphScriptNode`, `VoxelGraphScriptNodePort`, and `VoxelGraphScriptNodeParameter`; loads GDScript contracts; mirrors script-defined inputs, outputs, exported parameters, GLSL path, entry point, validation state, GPU compatibility, and CPU execution. | Yes |
| Class registration | `register_types.cpp` | Registers SGN resource classes so Godot can instantiate scripts, inspect resources, and serialize graph node parameters. | Yes |
| Node type registration | `generators/graph/nodes/misc.h` | Adds the `ScriptGraphNode` `NodeType`, creates the default SGN resource, compiles CPU runtime params, executes SGN per voxel, and marks range output as unbounded. | Yes |
| Node category | `generators/graph/node_type_db.h`, `generators/graph/nodes/misc.h` | Adds a `Misc` category and places `ScriptGraphNode` there instead of `Generate`. | Yes, requested UX/classification change |
| Dynamic graph behavior | `generators/graph/voxel_graph_function.h`, `generators/graph/voxel_graph_function.cpp`, `generators/graph/program_graph.cpp` | Adds `NODE_SCRIPT_GRAPH`, dynamic port setup, explicit script node refresh, connection preservation by port name, graph duplication support, serialization/deserialization of dynamic outputs, saved-port-name recovery, output-name lookup for SGN dynamic ports, graph hash invalidation by SGN revision, and safe connection checks while dynamic ports are being remapped. | Yes |
| Runtime compile/execution | `generators/graph/voxel_graph_compiler.cpp`, `generators/graph/voxel_graph_runtime.cpp` | Encodes SGN input/output counts in compiled operations because SGN ports are dynamic and cannot use static `NodeType` counts. Runtime, range analysis, and debug printing read those counts. | Yes |
| GPU shader generation | `generators/graph/voxel_graph_script_node.h`, `generators/graph/voxel_graph_script_node.cpp`, `generators/graph/voxel_graph_shader_generator.cpp`, `generators/graph/code_gen_helper.h`, `generators/graph/code_gen_helper.cpp` | `VoxelGraphScriptNode` reads SGN GLSL files, validates GPU compatibility, namespaces uniforms/functions per graph node, replaces SGN uniforms with parameter constants, and returns the shader function/source needed by the graph shader generator. The graph shader generator emits SGN calls and stores required shader library keys by string content instead of raw pointer address. | Yes for GPU SGN |
| Graph editor | `editor/graph/voxel_graph_editor.h`, `editor/graph/voxel_graph_editor.cpp` | Rebuilds SGN node layout when ports change, preserves GUI connections to and from dynamic ports, and guards preview updates from recursively reacting to graph changes. | Yes for editor workflow |
| Graph editor node UI | `editor/graph/voxel_graph_editor_node.h`, `editor/graph/voxel_graph_editor_node.cpp` | Shows the SGN script basename as a subtitle under the graph node title and refreshes it when the node changes. | Optional UX, low risk |
| Inspector wrapper | `editor/graph/voxel_graph_node_inspector_wrapper.cpp` | Exposes SGN script assignment, GLSL path, validation messages, exported parameters, and dynamic input defaults through the existing inspector wrapper and undo/redo flow. | Yes for editor workflow |
| Resource reload helper | `util/godot/classes/resource_loader.h`, `util/godot/classes/resource_loader.cpp` | Provides cache-replacing resource loads for explicit SGN script reload actions. | Yes for edit/reload workflow |
| Test fixtures | `project/tests/sgn_test_node.gd`, `project/tests/sgn_test_node.glsl` | Provides a small `@tool` SGN contract and matching GLSL implementation used by tests. | Yes |
| Tests and audit | `tests/voxel/test_voxel_graph.h`, `tests/voxel/test_voxel_graph.cpp`, `tests/tests.cpp`, `sgn_remake_audit.md` | Adds SGN tests for contract loading, clearing scripts, inspector-visible GLSL path, shader compilation, CPU execution, port refresh, and copy/reload connection preservation. Schedules them in the voxel test runner and records the cleaned patchset labels. | Yes |

## Changes Removed From The Effective Patch

These were present in the branch history and in the reference audit, but have been restored to `master` in the working tree because they are broader core reliability changes rather than SGN-specific implementation.

| Removed area | Files | Why removed | Follow-up recommendation |
| --- | --- | --- | --- |
| Terrain editor menu recompilation | `editor/terrain/voxel_terrain_editor_plugin.cpp` | This changed generic terrain editor regenerate behavior. SGN graph editor regeneration still handles SGN-oriented compile/reload. | Consider separately if manual terrain menu regeneration should always compile graph generators before restart. |
| GPU task runner device-attempt state | `engine/gpu/gpu_task_runner.cpp`, `engine/gpu/gpu_task_runner.h`, `engine/voxel_engine.h` | This is a general GPU warning UX improvement, not required for SGN classes, graph ports, or shader generation. | Separate GPU UX PR if renderer warnings are currently premature. |
| GPU generation failure propagation | `generators/generate_block_gpu_task.*`, `generators/generate_block_task.*`, `meshers/mesh_block_task.*` | This hardens invalid GPU shader/runtime failure paths globally. Good idea, but broad. | Separate core reliability PR for invalid shader RID handling and GPU failure propagation. |
| Stale task cancellation checks | `streams/load_block_data_task.cpp`, `generators/multipass/generate_block_multipass_cb_task.cpp`, `meshers/mesh_block_task.cpp` | These affect generic task result application. They are not necessary for SGN graph loading, editor editing, CPU execution, or shader generation. | Separate VLT/task lifecycle PR if stale results are reproducible after restarts. |
| VoxelLodTerrain dependency reset and cancellation tokens | `terrain/variable_lod/voxel_lod_terrain.cpp`, `terrain/variable_lod/voxel_lod_terrain_update_task.h`, `terrain/variable_lod/voxel_lod_terrain_update_clipbox_streaming.cpp` | This is central VLT lifecycle behavior and should not be carried by SGN unless strictly required. | Separate VLT restart correctness PR. |
| Generator graph built-in script property hiding | `generators/graph/voxel_generator_graph.*`, removed `VoxelGraphFunction::_validate_property` hook | This was inspector UX cleanup unrelated to SGN behavior. | Separate inspector polish PR if built-in `script` properties are confusing. |
| Old audit doc | `doc/sgn_plan.md` | Replaced by this audit so the patchset does not carry two overlapping review documents. | None. |

## SGN Implementation Standards Compared To Existing Nodes

SGN follows the existing graph architecture where practical:

- Node registration lives in `generators/graph/nodes/misc.h`, matching other node families that fill a `NodeType` entry with name, category, params, compile function, process function, range function, and shader behavior.
- The graph node stores its editable state in a hidden `NodeType::Param`, like `Function` nodes store a hidden `VoxelGraphFunction` resource. SGN uses a hidden `VoxelGraphScriptNode` resource because the contract is user-authored and dynamic.
- CPU execution uses the standard `compile_func` plus `process_buffer_func` path. The lambda converts graph buffers into dictionaries keyed by SGN port names, calls the configured GDScript entry point, then writes named outputs back to graph buffers.
- Range analysis is implemented in the same slot as other nodes. Returning infinite intervals is conservative and appropriate because arbitrary user GDScript cannot be range-analyzed safely.
- Shader generation plugs into the existing shader generator. SGN-specific GLSL loading, namespacing, uniform replacement, and entry-point validation live on `VoxelGraphScriptNode`; the upper shader generator only registers returned source and emits a typed call using dynamic port names.
- Editor properties use the existing `VoxelGraphNodeInspectorWrapper` pattern: `_get_property_list`, `_set`, `_get`, and `EditorUndoRedoManager` actions. Script assignment and parameter edits refresh dynamic graph ports and node layout, mirroring how expression/function node edits update graph structure.
- Serialization extends the existing dynamic input mechanism by adding dynamic output names. This is required because SGN output ports can be renamed/reordered by script changes, and saved numeric connections need a name-based recovery path.
- `ProgramGraph::can_connect` now rejects invalid port locations before checking duplicate/cycle state. SGN refresh can briefly reason about saved locations from an older port contract, so this guard is necessary to preserve same-named connections without invalid port access.

SGN necessarily has a few special cases:

- Compiled runtime operations include SGN input/output counts after the op id. Existing nodes can derive port counts from static `NodeType` definitions; SGN cannot.
- `VoxelGraphFunction` refreshes SGN ports by inspecting the SGN resource. This is analogous to function-node refresh, but uses SGN metadata instead of another graph's input/output definitions.
- Shader source inclusion requires `CodeGenHelper` to deduplicate by string content. SGN passes generated include names and source strings, so the previous raw `const char *` key storage was not safe for this use.

These special cases are localized to dynamic graph plumbing and are justified by SGN's user-scripted port contract. They do not require changing terrain, streaming, meshing, or GPU task scheduling.

## Necessary Versus Optional Remaining Changes

Required for a useful SGN PR:

- SGN resource contract and validation.
- SGN class registration.
- `NODE_SCRIPT_GRAPH` enum and node type registration.
- Dynamic SGN port setup, refresh, serialization, deserialization, duplication, and connection preservation.
- Runtime compiler/runtime count handling for dynamic SGN ports.
- Safe `ProgramGraph` connection checks for dynamic SGN port remapping.
- CPU SGN execution.
- GPU SGN shader source generation and GLSL validation, owned by `VoxelGraphScriptNode`.
- Inspector script/path/parameter editing with undo/redo.
- Explicit reload hooks for GDScript/GLSL backing files.
- Test fixtures and scheduled SGN tests.
- Requested `Misc` category.

Optional but acceptable in this SGN PR:

- Graph node subtitle showing the SGN script basename. This is editor-only and helps users identify which script a node uses. It can be removed if the PR needs to be even smaller.
- Validation error/warning arrays in the SGN node inspector. They are editor-only and directly tied to SGN usability.

Not suitable for this SGN PR:

- GPU task failure propagation.
- VLT cancellation/dependency lifecycle changes.
- Generic graph-core defensive checks beyond the SGN dynamic-port guard.
- Terrain editor menu behavior changes outside the SGN graph editor.
- Broad inspector cleanup for non-SGN resources.

## Test Coverage

Existing and new SGN coverage is located in `tests/voxel/test_voxel_graph.cpp` and declared in `tests/voxel/test_voxel_graph.h`.

Current SGN tests include:

- `test_voxel_graph_sgn_load_script_contract`
- `test_voxel_graph_sgn_clear_script_reverts_ports`
- `test_voxel_graph_sgn_shader_path_property`
- `test_voxel_graph_sgn_shader_compilation`
- `test_voxel_graph_script_node_contract`
- `test_voxel_graph_script_node_cpu_execution`
- `test_voxel_graph_script_node_debug_compile_does_not_emit_changed`
- `test_voxel_graph_script_node_port_refresh`
- `test_voxel_graph_script_node_copy_keeps_connections_after_reload`

They are scheduled in `tests/tests.cpp` and use `project/tests/sgn_test_node.gd` plus `project/tests/sgn_test_node.glsl`.

## Final Review Cleanup Notes

- Removed the extra `GraphEdit::has_node()` guard from `VoxelGraphEditor::update_node_layout()`. If graph changes are emitted while the editor GUI is being rebuilt or after it is cleared, that should now surface through the existing typed-node lookup failure instead of being silently ignored.
- Replaced editor-side SGN type checks for layout refresh with generic node metadata: node types now declare whether parameter changes affect layout and whether rebuilt layouts should fit their content. `ScriptGraphNode` sets both flags.
- Removed automatic source-file change listening; SGN scripts are refreshed through explicit reload actions instead.
- Renamed the graph-node subtitle update path from script-specific terminology to generic subtitle terminology. The graph now supplies the subtitle text, currently the SGN script basename.
- Replaced terrain-type downcasts in graph editor regeneration with `VoxelNode::is_generator_using_gpu()`, overridden by fixed and variable LOD terrains when GPU generation is compiled in.
- Kept the preview recursion guard, but renamed its local RAII helper and documented why graph `changed` notifications are ignored during preview compilation.

## Verification Notes

After trimming the patchset, the editor build succeeded with:

```text
scons platform=windows target=editor voxel_tests=yes
```

The full project test scene was run with:

```text
bin\godot.windows.editor.dev.x86_64.console.exe --path modules\voxel\project res://tests/runner.tscn
```

The runner reached the expected `------------ Voxel tests end -------------` marker. The run included:

- `test_voxel_graph_sgn_load_script_contract`
- `test_voxel_graph_sgn_clear_script_reverts_ports`
- `test_voxel_graph_sgn_shader_path_property`
- `test_voxel_graph_sgn_shader_compilation`
- `test_voxel_graph_script_node_contract`
- `test_voxel_graph_script_node_cpu_execution`
- `test_voxel_graph_script_node_debug_compile_does_not_emit_changed`
- `test_voxel_graph_script_node_port_refresh`
- `test_voxel_graph_script_node_copy_keeps_connections_after_reload`
- `test_voxel_graph_editor_create_dynamic_layout_node`

Final review cleanup verification:

- `scons platform=windows target=editor voxel_tests=yes dev_build=yes debug_symbols=yes` passed.
- `bin\godot.windows.editor.dev.x86_64.console.exe --path modules\voxel\project res://tests/runner.tscn` reached the expected test-end marker. The command does not exit by itself, so the verification wrapper stopped the process after observing the marker.

## Review Checklist

- Confirm no broad terrain, streaming, meshing, or GPU task lifecycle changes remain in the effective diff.
- Confirm `ScriptGraphNode` appears under `Misc`.
- Confirm SGN scripts load through `SGN Script` and expose expected dynamic ports and exported parameters.
- Confirm clearing `SGN Script` leaves the graph node as an empty `ScriptGraphNode` with no stale ports.
- Confirm GDScript and GLSL file reload refreshes validation and graph node layout.
- Confirm CPU compilation/execution succeeds for valid SGN.
- Confirm shader source generation succeeds for valid SGN GLSL and uses per-node namespacing.
- Confirm serialization/deserialization preserves dynamic port names and reconnects same-named ports.
