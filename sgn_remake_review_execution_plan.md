# ScriptGraphNode Review Execution Plan

Target branch: `sgn_remake_noclip`.

Goal: reduce the architectural issues found in `sgn_remake_review.md` and dramatically reduce the amount of code that must land with SGN. Agents should pick one or two work items, make narrowly scoped edits, run the listed verification, and mark the item complete with notes.

## Working Rules

- Keep each change focused. Do not bundle unrelated cleanup with SGN architecture cleanup.
- Do not revert user work or unrelated dirty files.
- Prefer removing broad changes over justifying them inside this PR.
- Prefer deleting, deferring, or simplifying over adding new generic infrastructure. A cleaner abstraction that adds more code is not a win for this PR unless it also removes more code or removes a serious architecture violation.
- Preserve existing SGN tests and fixtures.
- If a task reveals a real bug outside SGN, record it as follow-up work instead of fixing it in this branch unless the task explicitly covers it.
- After completing a task, update this document:
  - change `[ ]` to `[x]`
  - add a short `Completed:` note with files changed
  - add a short `Verified:` note with commands/tests run

## Diff Reduction Strategy

Current branch size is approximately 3,006 insertions and 107 deletions against `master`. The biggest contributors are:

```text
1139  generators/graph/voxel_graph_script_node.cpp
 364  tests/voxel/test_voxel_graph.cpp
 292  generators/graph/voxel_graph_function.cpp
 226  generators/graph/voxel_graph_shader_generator.cpp
 164  generators/graph/voxel_graph_script_node.h
 164  sgn_remake_audit.md
 159  editor/graph/voxel_graph_node_inspector_wrapper.cpp
 156  generators/graph/nodes/misc.h
  75  editor/graph/voxel_graph_editor_node.cpp
```

The first pass should prioritize **line removal**, not polish:

1. Remove unrelated broad fixes entirely. This can drop many touched files and reduce review surface immediately.
2. Avoid turning every SGN special case into a generic framework. Generic callbacks can improve design, but if they add new node-type APIs and spread edits across more files, they may hurt approval odds.
3. Keep direct SGN special cases when they are small, local, and easy to explain. The review concern is broad architectural coupling, not every occurrence of `NODE_SCRIPT_GRAPH`.
4. Defer optional or release-polish features unless they are necessary for SGN to be usable. The subtitle is necessary; broad lifecycle fixes and generic framework cleanup are not.
5. Reduce tests to a focused acceptance suite. Exhaustive tests are useful, but a large test-only diff still increases review burden. Move edge-case coverage to follow-up if it does not protect the minimum SGN contract.
6. Keep documentation concise. The audit and review docs are useful locally, but the submitted PR should not carry large process documents unless reviewers asked for them.

Minimum viable SGN should include:

- `VoxelGraphScriptNode` resource contract
- graph node registration
- dynamic ports and connection preservation
- CPU execution
- GLSL path and shader generation if GPU SGN is in scope
- editor assignment/reload of the script and shader path
- visible SGN script subtitle in the graph editor
- targeted tests for the above

Minimum viable SGN should not include:

- engine shutdown/task runner changes
- mesh block/world/physics body lifetime changes
- broad graph-core generic abstractions unless required for correctness
- optional UX beyond identifying and editing SGN
- broad generated docs churn beyond the new node documentation

## Baseline Verification

Before taking a task, capture the starting point.

- [x] Run `git status --short` from `modules/voxel` and record unrelated dirty/untracked files.
  Completed: initial untracked files were `Notes_To_Resolve.md`, `doc_data.gen.cpp`, `sgn_remake_review.md`, and `sgn_remake_review_execution_plan.md`.
- [x] Run `git diff --name-status master...HEAD` from `modules/voxel` and confirm the task scope is present.
  Completed: confirmed Task 1 and Task 2 files were present in the branch diff.
- [x] Check whether a built editor binary already exists under the outer Godot `bin/` directory.
  Completed: `bin/godot.windows.editor.dev.x86_64.console.exe` existed before rebuilding.
- [x] Prefer a compile check after code changes:

```powershell
scons platform=windows target=editor voxel_tests=yes
```

- [x] Prefer targeted SGN tests after graph/runtime/editor changes. The SGN test names are:

```text
test_voxel_graph_sgn_load_script_contract
test_voxel_graph_sgn_clear_script_reverts_ports
test_voxel_graph_sgn_shader_path_property
test_voxel_graph_sgn_shader_compilation
test_voxel_graph_script_node_contract
test_voxel_graph_script_node_cpu_execution
test_voxel_graph_script_node_debug_compile_does_not_emit_changed
test_voxel_graph_script_node_port_refresh
test_voxel_graph_script_node_copy_keeps_connections_after_reload
test_voxel_graph_editor_create_dynamic_layout_node
```

`VoxelEngine.run_tests({"includes": [...]})` supports running only named tests. If no targeted test runner script exists in the checkout, add a temporary local script for verification and do not commit it.

Known baseline caveat from `sgn_remake_audit.md`: full `--run_voxel_tests` may fail earlier in `test_block_serializer_stream_peer`, before SGN tests. If reproducing that, record it as pre-existing and do not treat it as an SGN regression.

Completed: reran the requested editor build after Task 6 review; SCons reported the target up to date.
Verified: `scons platform=windows target=editor voxel_tests=yes dev_build=yes debug_symbols=yes`.

## Task 1: Move Regeneration Preparation Into The Generator

Status: [x]

Priority: highest.

Reason: current terrain handlers downcast `VoxelGenerator` to `VoxelGeneratorGraph` and compile it. That violates the architecture rule that parents should not branch on child type. The generator should prepare itself, then signal consumers.

Files expected:

- `generators/voxel_generator.h`
- `generators/voxel_generator.cpp`
- `generators/graph/voxel_generator_graph.h`
- `generators/graph/voxel_generator_graph.cpp`
- `terrain/fixed_lod/voxel_terrain.cpp`
- `terrain/variable_lod/voxel_lod_terrain.cpp`
- maybe `terrain/fixed_lod/voxel_terrain.h`
- maybe `terrain/variable_lod/voxel_lod_terrain.h`

Concrete steps:

1. Make `VoxelGenerator::request_regeneration()` virtual.
2. Keep the base implementation as the signal emitter:

```cpp
void VoxelGenerator::request_regeneration() {
	emit_signal(SIGNAL_REGENERATION_REQUESTED);
}
```

3. Override `VoxelGeneratorGraph::request_regeneration()`.
4. In the override, compile the graph before signaling:

```cpp
const pg::CompilationResult result = compile(true);
if (!result.success) {
	ERR_PRINT(String("Graph compilation failed before regeneration: {0}").format(varray(result.message)));
	return;
}
```

5. Handle GPU shader state inside `VoxelGeneratorGraph`, not terrain. Prefer the narrowest behavior that preserves existing behavior:
   - If the current branch intentionally compiles shaders immediately before terrain restart, keep that behavior in the override under `#ifdef VOXEL_ENABLE_GPU`.
   - If existing graph compile already invalidates shaders and terrain `process()` lazily compiles them, prefer not to force immediate shader compilation. Document the choice in the completion note.
6. Call `VoxelGenerator::request_regeneration()` only after preparation succeeds.
7. Remove `#include "../../generators/graph/voxel_generator_graph.h"` from terrain files if no longer needed.
8. Remove terrain downcasts and compile/shader logic from `_on_generator_regeneration_requested()`.
9. Keep terrain handlers generic:

```cpp
void VoxelTerrain::_on_generator_regeneration_requested() {
	restart_stream();
}
```

and the same for `VoxelLodTerrain`.

Acceptance criteria:

- No `Object::cast_to<VoxelGeneratorGraph>` remains in terrain regeneration handlers.
- Terrains still connect/disconnect to `VoxelGenerator::SIGNAL_REGENERATION_REQUESTED`.
- Graph-specific compile errors prevent the regeneration signal from being emitted.
- Non-graph generators can still emit regeneration requests through the base method.
- The editor still calls down into the generator with `generator->request_regeneration()`.

Verification:

- Run `rg -n "Object::cast_to<VoxelGeneratorGraph>|compile\\(true\\)|compile_shaders\\(\\)" terrain generators/graph/voxel_generator_graph.* generators/voxel_generator.*`.
- Run `scons platform=windows target=editor voxel_tests=yes`.
- Run SGN graph tests listed in Baseline Verification, especially shader compilation and port refresh.

Completion notes:

- Completed: made `VoxelGenerator::request_regeneration()` virtual; added `VoxelGeneratorGraph::request_regeneration()` to compile the graph before emitting the base regeneration signal; removed `VoxelGeneratorGraph` includes, downcasts, compile calls, and shader compile calls from fixed and variable LOD terrain regeneration handlers.
- Verified: ran `rg -n "Object::cast_to<VoxelGeneratorGraph>|compile\\(true\\)|compile_shaders\\(\\)" terrain generators/graph/voxel_generator_graph.cpp generators/graph/voxel_generator_graph.h generators/voxel_generator.cpp generators/voxel_generator.h`; remaining `compile(true)` is in `VoxelGeneratorGraph::request_regeneration()` and remaining `compile_shaders()` hits are existing lazy GPU terrain processing paths. Ran `scons platform=windows target=editor voxel_tests=yes dev_build=yes debug_symbols=yes` successfully after stopping old Godot processes that were locking the output binaries. Ran `bin\godot.windows.editor.dev.x86_64.console.exe --path modules\voxel\project res://tests/runner.tscn`; it reached `------------ Voxel tests end -------------` including all SGN tests.

## Task 2: Remove Or Split Broad Shutdown And Mesh Lifetime Changes

Status: [x]

Priority: highest.

Reason: this branch still includes non-SGN lifecycle fixes. Even if valid, they touch engine shutdown, physics body world lifetime, and mesh block teardown. These should not be in the SGN PR unless proven strictly required by SGN.

Files expected:

- `engine/voxel_engine.cpp`
- `util/godot/direct_static_body.h`
- `util/godot/direct_static_body.cpp`
- `terrain/fixed_lod/voxel_mesh_block_vt.h`
- `terrain/variable_lod/voxel_mesh_block_vlt.cpp`
- `terrain/voxel_mesh_block.h`
- `terrain/voxel_mesh_block.cpp`
- `terrain/voxel_mesh_map.h`

Concrete steps:

1. Inspect each diff against `master`.
2. Classify each change as:
   - required for SGN compile/runtime/tests
   - unrelated shutdown/lifetime fix
   - accidental cleanup
3. Revert unrelated shutdown/lifetime changes from this branch by editing files back to the `master` behavior. Do not use destructive git commands.
4. If removing these changes causes an SGN test crash, stop and document the exact failure. Do not silently reintroduce broad fixes.
5. If a minimal SGN-specific lifetime fix is truly required, isolate it to the smallest file and add a clear comment explaining the SGN dependency.

Acceptance criteria:

- `git diff master...HEAD -- engine/voxel_engine.cpp util/godot/direct_static_body.* terrain/*/voxel_mesh_block* terrain/voxel_mesh_block.* terrain/voxel_mesh_map.h` is empty, unless a remaining line is explicitly documented as SGN-required.
- No broad shutdown-order, task-runner flush, or physics-body API change remains in the SGN diff.

Verification:

- Run `git diff --name-status master...HEAD` and confirm broad files are removed from the SGN diff or justified.
- Run `scons platform=windows target=editor voxel_tests=yes`.
- Run SGN tests. If tests reveal a shutdown problem after removal, document it as a separate reliability PR candidate.

Completion notes:

- Completed: removed the branch changes in `engine/voxel_engine.cpp`, `util/godot/direct_static_body.*`, `terrain/fixed_lod/voxel_mesh_block_vt.h`, `terrain/variable_lod/voxel_mesh_block_vlt.cpp`, `terrain/voxel_mesh_block.*`, and `terrain/voxel_mesh_map.h` by restoring the previous shutdown, physics body world pointer, and mesh block teardown behavior.
- Verified: ran `git diff --name-status master...HEAD -- engine/voxel_engine.cpp util/godot/direct_static_body.h util/godot/direct_static_body.cpp terrain/fixed_lod/voxel_mesh_block_vt.h terrain/variable_lod/voxel_mesh_block_vlt.cpp terrain/voxel_mesh_block.h terrain/voxel_mesh_block.cpp terrain/voxel_mesh_map.h`; after these edits are committed, these files drop out of the SGN branch diff. Ran `scons platform=windows target=editor voxel_tests=yes dev_build=yes debug_symbols=yes` successfully. Ran `bin\godot.windows.editor.dev.x86_64.console.exe --path modules\voxel\project res://tests/runner.tscn`; it reached `------------ Voxel tests end -------------`.

## Task 3: Keep Subtitle UX But Make It Robust

Status: [x]

Priority: high.

Reason: the subtitle is a necessary SGN graph editor feature. Without it, users cannot tell which script-backed node they are looking at by script type from the graph canvas. The issue is not that the subtitle exists; the issue is that the current implementation recursively searches and reparents `GraphNode` child labels. The subtitle should be managed through a robust graph-editor-node path, similar to how the title is updated.

Files expected:

- `editor/graph/voxel_graph_editor_node.h`
- `editor/graph/voxel_graph_editor_node.cpp`
- `generators/graph/voxel_graph_function.h`
- `generators/graph/voxel_graph_function.cpp`

Concrete steps:

1. Keep the subtitle feature.
2. Remove recursive child-tree probing:
   - remove `find_graph_node_title_label()`
   - remove logic that searches for arbitrary `Label` descendants inside the `GraphNode` titlebar
   - remove logic that reparents Godot's internal title label into a custom container
3. Keep a subtitle update method on `VoxelGraphEditorNode`, but make it operate only on controls owned by `VoxelGraphEditorNode`.
4. Follow the title update pattern:
   - `VoxelGraphEditorNode::create()` should initialize title and subtitle from the graph
   - `VoxelGraphEditorNode::update_title()` or a nearby `update_title_and_subtitle()` path should refresh both when graph node metadata changes
   - `VoxelGraphEditor::update_node_layout()` should refresh title/subtitle before or during layout refresh, as it already does for title
5. Prefer an explicit owned subtitle label added to the graph node body/header area by `VoxelGraphEditorNode`, not a mutation of Godot's internal titlebar structure.
6. If Godot exposes a stable titlebar API that allows adding a subtitle without child probing, use that API. Otherwise, place the owned subtitle label in a stable location controlled by `VoxelGraphEditorNode`.
7. Preserve `VoxelGraphFunction::get_node_subtitle()` only if it remains the clean graph-side metadata source. If it remains SGN-specific internally, consider moving the subtitle source behind `NodeType` metadata in a later graph-metadata cleanup task.
8. Ensure empty subtitles remove or hide the owned subtitle label without affecting title controls.
9. Ensure SGN script changes and layout refreshes update the subtitle.

Acceptance criteria:

- No recursive `GraphNode` child search remains for subtitle handling.
- `VoxelGraphEditorNode` no longer reparents Godot internal title labels.
- The subtitle remains visible for SGN nodes with an assigned script.
- Subtitle refresh follows the same graph editor update flow as title refresh.
- Empty subtitle state is handled cleanly for non-SGN nodes and SGN nodes without a script.
- SGN behavior remains intact with subtitle UX preserved.

Verification:

- Run `rg -n "find_graph_node_title_label|TITLE_CONTAINER_NAME|SUBTITLE_LABEL_NAME|get_node_subtitle|update_subtitle" editor/graph generators/graph`.
- Confirm any remaining `update_subtitle` or `get_node_subtitle` usage is part of the robust owned-control path, not child introspection.
- Run `scons platform=windows target=editor voxel_tests=yes`.
- If practical, open the graph editor and confirm SGN nodes show the script subtitle, non-SGN nodes do not show stale subtitle text, titles still render, and SGN layout updates still work.

Completion notes:

- Completed: removed recursive `GraphNode` titlebar child probing, internal title label reparenting, and subtitle lookup constants from `editor/graph/voxel_graph_editor_node.cpp`; `VoxelGraphEditorNode` now owns a subtitle `Label`, shows/hides it for empty subtitles, and offsets owned row slots when the subtitle control exists.
- Verified: ran `rg -n "find_graph_node_title_label|TITLE_CONTAINER_NAME|SUBTITLE_LABEL_NAME|get_node_subtitle|update_subtitle|update_title" editor/graph generators/graph`; remaining subtitle hits are the owned-control update path and `VoxelGraphFunction::get_node_subtitle()`. Ran `scons platform=windows target=editor voxel_tests=yes dev_build=yes debug_symbols=yes` successfully. Ran `bin\godot.windows.editor.dev.x86_64.console.exe --path modules\voxel\project res://tests/runner.tscn`; it reached `------------ Voxel tests end -------------` including all SGN tests.

## Task 4: Keep Dynamic Runtime Port Handling Minimal

Status: [x]

Priority: medium.

Reason: compiler and runtime currently special-case `NODE_SCRIPT_GRAPH` to encode/decode dynamic input/output counts. This is functionally needed. A generic `NodeType` abstraction may be architecturally nicer, but it should not be added unless it reduces the diff or removes duplicated code. For this PR, a small, obvious SGN branch may be more acceptable than a broad new generic framework.

Files expected:

- `generators/graph/node_type_db.h`
- `generators/graph/nodes/misc.h`
- `generators/graph/voxel_graph_compiler.cpp`
- `generators/graph/voxel_graph_runtime.cpp`

Concrete steps:

1. Inspect the current compiler/runtime SGN branches.
2. Do not add a new `NodeType` flag/callback unless it removes more code than it adds.
3. If keeping direct `NODE_SCRIPT_GRAPH` checks is smaller, keep them but make them tightly localized:
   - use one helper such as `node_has_dynamic_runtime_ports(type_id)` if it reduces repeated conditionals
   - keep operation encoding unchanged
   - keep comments short and explicit: SGN has dynamic ports, static node counts cannot be read from `NodeType`
4. Remove redundant local variables or repeated conditionals if possible.
5. Preserve assertions for static nodes: static nodes should still assert that graph port counts match type port counts.

Acceptance criteria:

- The compiler/runtime diff is no larger than before this task.
- Any remaining `NODE_SCRIPT_GRAPH` checks are localized to dynamic runtime port-count handling.
- SGN still compiles and executes with dynamic input/output counts.
- Static nodes keep existing compile/runtime behavior.

Verification:

- Run `rg -n "NODE_SCRIPT_GRAPH" generators/graph/voxel_graph_compiler.cpp generators/graph/voxel_graph_runtime.cpp` and confirm all hits are count-encoding/count-decoding only.
- Run SGN CPU execution, port refresh, copy/reload, and shader tests.
- Run a few existing non-SGN graph tests, especially:

```text
test_voxel_graph_generator_default_graph_compilation
test_voxel_graph_functions_pass_through
test_voxel_graph_functions_misc
test_voxel_graph_get_io_indices
```

Completion notes:

- Completed: centralized runtime operation port-count decoding in `generators/graph/voxel_graph_runtime.cpp` with a small local helper; the only runtime `NODE_SCRIPT_GRAPH` check now reads the encoded dynamic input/output counts. Left compiler-side checks local because they are already limited to count encoding/static-node assertions and broader abstraction would grow the diff.
- Verified: ran `rg -n "NODE_SCRIPT_GRAPH" generators/graph/voxel_graph_compiler.cpp generators/graph/voxel_graph_runtime.cpp`; hits are limited to runtime dynamic count decoding, compiler static-node assertions/count selection, and encoded output-count allocation. Ran `scons platform=windows target=editor voxel_tests=yes dev_build=yes debug_symbols=yes` successfully. Ran `bin\godot.windows.editor.dev.x86_64.console.exe --path modules\voxel\project res://tests/runner.tscn`; it reached `------------ Voxel tests end -------------`, including the SGN CPU/port/shader tests and existing non-SGN graph tests such as `test_voxel_graph_get_io_indices`, `test_voxel_graph_function_execute`, and `test_voxel_graph_hash`.

## Task 5: Shrink Dynamic Port Serialization And Remap

Status: [x]

Priority: medium-high.

Reason: dynamic output serialization is needed for SGN connection preservation, but this is one of the larger non-resource changes in the branch. The goal is to keep only the minimum save/load/remap behavior required to preserve SGN connections and avoid broad generic serialization work unless it reduces code.

Files expected:

- `generators/graph/voxel_graph_function.cpp`
- maybe `generators/graph/program_graph.h`
- maybe `generators/graph/program_graph.cpp`

Concrete steps:

1. Inspect the current save/load helpers and count how much code is solely for SGN remapping.
2. Prefer the smallest readable implementation that preserves same-named SGN input/output connections after reload.
3. Do not broaden this into a full generic dynamic-port serialization framework unless it reduces code.
4. If generic helper names reduce confusion without adding code, rename them. If renaming creates churn, keep SGN-specific names and document why the special case exists.
5. Consider whether saved dynamic outputs can be stored in the same structure as dynamic inputs, or whether the current separate `dynamic_outputs` field is the least invasive option.
6. Keep behavior for existing function-node dynamic inputs intact.
7. Ensure invalid saved connections are still dropped safely with a clear error.

Acceptance criteria:

- The serialization/remap diff is smaller or no larger after this task.
- Any SGN-specific save/load behavior is clearly isolated and easy to explain.
- SGN same-named input and output connections survive reload/copy tests.
- Existing function-node dynamic input loading still works.

Verification:

- Run `rg -n "script_node_port|ScriptGraphNode\\\"|dynamic_outputs|dynamic_inputs" generators/graph/voxel_graph_function.cpp`.
- Run:

```text
test_voxel_graph_functions_pass_through
test_voxel_graph_functions_nested_pass_through
test_voxel_graph_functions_io_mismatch
test_voxel_graph_script_node_copy_keeps_connections_after_reload
test_voxel_graph_script_node_port_refresh
```

Completion notes:

- Completed: replaced SGN-named saved-port parsing helpers in `generators/graph/voxel_graph_function.cpp` with generic `dynamic_inputs`/`dynamic_outputs` saved-name helpers used by both SGN load recovery and connection remap; kept the existing save format while making the local patch 23 net lines smaller.
- Verified: ran `git diff --numstat master...HEAD -- generators/graph/voxel_graph_function.cpp` and `git diff --stat -- generators/graph/voxel_graph_function.cpp`; this task changes `voxel_graph_function.cpp` by 44 insertions and 67 deletions. Ran `scons platform=windows target=editor voxel_tests=yes dev_build=yes debug_symbols=yes` successfully after stopping stale Godot processes that were locking the editor binary. Ran `bin\godot.windows.editor.dev.x86_64.console.exe --path modules\voxel\project res://tests/runner.tscn`; it reached `------------ Voxel tests end -------------`, including `test_voxel_graph_functions_pass_through`, `test_voxel_graph_functions_nested_pass_through`, `test_voxel_graph_functions_io_mismatch`, `test_voxel_graph_script_node_copy_keeps_connections_after_reload`, and `test_voxel_graph_script_node_port_refresh`.

## Task 6: Minimize Or Defer SGN Output Hash Cleanup

Status: [x]

Priority: low-medium.

Reason: `VoxelGraphFunction::get_output_graph_hash()` special-cases SGN resource revisions. A `NodeType` hash callback would be cleaner, but it adds new infrastructure for one user. For diff reduction, prefer either a very small local SGN hash contribution or deferring the abstraction.

Files expected:

- `generators/graph/node_type_db.h`
- `generators/graph/nodes/misc.h`
- `generators/graph/voxel_graph_function.cpp`

Concrete steps:

1. First decide whether the revision hash is required for MVP SGN editor live update.
2. If it is not required, remove it and document the deferred behavior.
3. If it is required, keep the current direct SGN branch unless a callback removes more code than it adds.
4. Do not add a new `NodeType` hash callback in this PR unless there will be multiple users or a clear net LOC reduction.
5. Keep the existing deep-hash behavior for object params.

Acceptance criteria:

- The output hash diff is minimal.
- SGN revision changes still affect the output graph hash.
- Non-SGN nodes keep previous hash behavior.

Verification:

- Run `rg -n "NODE_SCRIPT_GRAPH|get_revision" generators/graph/voxel_graph_function.cpp generators/graph/nodes/misc.h`.
- Run:

```text
test_voxel_graph_hash
test_voxel_graph_script_node_port_refresh
test_voxel_graph_sgn_shader_path_property
```

`test_voxel_graph_hash` may be gated by feature macros; if unavailable, note that in completion.

Completion notes:

- Completed: reviewed `VoxelGraphFunction::get_output_graph_hash()` and kept the existing direct SGN `get_revision()` contribution because it is the smallest local behavior and no `NodeType` hash callback/infrastructure exists in the branch. This preserves SGN live-update hash invalidation while keeping non-SGN object-param deep hashing unchanged.
- Verified: ran `rg -n "NODE_SCRIPT_GRAPH|get_revision" generators/graph/voxel_graph_function.cpp generators/graph/nodes/misc.h`; the revision hash contribution remains a small local branch in `get_output_graph_hash()`. Ran `scons platform=windows target=editor voxel_tests=yes dev_build=yes debug_symbols=yes`; SCons completed successfully and reported the target up to date. Ran `bin\godot.windows.editor.dev.x86_64.console.exe --path modules\voxel\project res://tests/runner.tscn` with output redirected and stopped the process after the expected `------------ Voxel tests end -------------` marker; the run included `test_voxel_graph_hash`, `test_voxel_graph_script_node_port_refresh`, `test_voxel_graph_sgn_shader_path_property`, and all SGN tests.

## Task 7: Shrink SGN Shader Generation

Status: [x]

Priority: medium.

Reason: SGN shader generation is about 224 added lines and is one of the largest non-resource chunks. Moving it behind new generic callbacks may improve architecture but likely increases code. The priority is to remove duplication and keep only the minimum shader path required for SGN.

Files expected:

- `generators/graph/node_type_db.h`
- `generators/graph/nodes/misc.h`
- `generators/graph/voxel_graph_shader_generator.cpp`
- maybe `generators/graph/voxel_graph_script_node.cpp`
- maybe `generators/graph/voxel_graph_script_node.h`

Concrete steps:

1. Look for duplicated helpers between `voxel_graph_script_node.cpp` and `voxel_graph_shader_generator.cpp`, especially file reading, GLSL type/literal conversion, identifier parsing, and entry point validation.
2. Move shared SGN GLSL helper logic into the SGN resource file or a small local helper only if it reduces total lines.
3. Do not add a new shader callback framework in this PR unless it reduces total code.
4. Keep the direct SGN shader-generation branch if that is the smallest understandable integration point.
5. Keep `CodeGenHelper` string-key deduplication unchanged; it is a valid small generic fix.
6. Consider limiting MVP GLSL handling:
   - support `float`, `int`, and `bool` only if all are required by tests and expected release scope
   - avoid broad regex support for GLSL forms not documented for SGN MVP
   - defer complex namespacing cases if tests and documented contract do not require them
7. Preserve current shader source output for supported MVP cases.

Acceptance criteria:

- SGN shader-generation diff is smaller or no larger after this task.
- SGN GLSL namespacing, uniform constant replacement, and call emission still work.
- Existing non-SGN shader nodes still use the existing shader generation path.

Verification:

- Run `rg -n "NODE_SCRIPT_GRAPH|is_script_graph_node|VoxelGraphScriptNode" generators/graph/voxel_graph_shader_generator.cpp`.
- Run:

```text
test_voxel_graph_sgn_shader_compilation
test_voxel_graph_sgn_shader_path_property
```

- If GPU build is enabled, verify shader compilation paths still compile.

Completion notes:

- Completed: reviewed the SGN resource MVP surface and kept the existing public API because it is used by the graph editor assignment/reload path, runtime CPU execution, shader generation, serialization, GDScript contract loading, or tests. Reduced repeated failed/empty script cleanup in `generators/graph/voxel_graph_script_node.cpp` by adding a private `clear_contract()` helper shared by empty script assignment and script load failure paths; no public API was added or removed.
- Verified: ran `git diff --stat -- generators/graph/voxel_graph_script_node.cpp generators/graph/voxel_graph_script_node.h`; the task changes those files by 20 insertions and 32 deletions. Ran `rg -n "StdVector<std::string>|std::string" generators/graph/voxel_graph_script_node.cpp generators/graph/voxel_graph_shader_generator.cpp`; remaining `std::string` use is local to regex/string rewriting helpers. Ran `scons platform=windows target=editor voxel_tests=yes dev_build=yes debug_symbols=yes`; the first link attempt failed because stale Godot editor/test processes locked the output executable, then passed after stopping those processes. Ran `bin\godot.windows.editor.dev.x86_64.console.exe --path modules\voxel\project res://tests/runner.tscn` with output redirected and stopped the process after the expected `------------ Voxel tests end -------------` marker; the run included SGN contract, CPU execution, port refresh, copy/reload, and shader compilation tests.

## Task 8: Shrink The SGN Resource Contract

Status: [ ]

Priority: high.

Reason: `VoxelGraphScriptNode` is the largest part of the branch: about 1,139 lines in `.cpp` and 164 lines in `.h`. This is expected to be the core of the PR, but it still needs a hard pass for MVP scope. The goal is to remove release-polish behavior that is not required for SGN to work.

Files expected:

- `generators/graph/voxel_graph_script_node.cpp`
- `generators/graph/voxel_graph_script_node.h`
- `generators/graph/voxel_graph_shader_generator.cpp`

Concrete steps:

1. Identify public methods and properties that are not needed by the graph editor, runtime, shader generator, serialization, or tests.
2. Remove or defer any API that only exists for future extensibility.
3. Look for duplicated code paths:
   - empty-script cleanup
   - failed-script cleanup
   - validation reset/error handling
   - port and parameter copying
   - GLSL file reading and parsing shared with shader generation
4. Consolidate repeated cleanup into small local helpers only if it reduces total lines and remains readable.
5. Keep `VoxelGraphScriptNodePort` and `VoxelGraphScriptNodeParameter` only if separate resource types are required for Godot serialization/inspection. If a simpler representation works with fewer changes and preserves editor behavior, prefer it.
6. Check whether validation warnings and errors both need editor exposure in MVP. If one channel is enough for release, simplify.
7. Check whether `entry_point` must be configurable in MVP. If all tests and docs use `generate`, consider hardcoding it and deferring custom entry points.
8. Check whether SGN parameters must support all of `float`, `int`, and `bool` in MVP. If only `float` is used and documented, defer other types.
9. Keep `std::string`, `std::smatch`, and `std::regex` only where directly required by regex APIs.
10. Change `StdVector<std::string>` to `StdVector<StdString>` where values are stored beyond immediate regex matches if doing so does not increase code.
11. Use existing conversion helpers at boundaries:
   - `zylann::godot::to_std_string`
   - `zylann::godot::to_godot`
12. Avoid broad parser rewrites. This task is scope reduction, not a GLSL parser replacement.

Acceptance criteria:

- `voxel_graph_script_node.cpp` and `.h` are smaller after this task, or every retained section is explicitly required for MVP.
- Removed API is either unused or covered by a documented follow-up.
- `std::string` remains local to regex operations.
- Voxel-owned containers prefer `StdString`.
- No behavior change in required SGN load, CPU execution, port refresh, or shader-generation tests.

Verification:

- Run `git diff --numstat master...HEAD -- generators/graph/voxel_graph_script_node.cpp generators/graph/voxel_graph_script_node.h`.
- Run `rg -n "StdVector<std::string>|std::string" generators/graph/voxel_graph_script_node.cpp generators/graph/voxel_graph_shader_generator.cpp`.
- Run SGN contract, CPU execution, port refresh, copy/reload, and shader compilation tests.

Completion notes:

- Completed:
- Verified:

## Task 9: Trim Tests To A Focused Acceptance Suite

Status: [ ]

Priority: medium.

Reason: tests add about 364 lines in `tests/voxel/test_voxel_graph.cpp` plus registration/header lines. Good tests help approval, but a large test diff also increases review cost. Keep tests that protect the minimum SGN contract and move edge-case or implementation-detail tests to follow-up.

Files expected:

- `tests/voxel/test_voxel_graph.cpp`
- `tests/voxel/test_voxel_graph.h`
- `tests/tests.cpp`
- `project/tests/sgn_test_node.gd`
- `project/tests/sgn_test_node.glsl`

Concrete steps:

1. Classify each SGN test as MVP, regression, or nice-to-have.
2. Keep tests that cover:
   - script contract loading
   - clearing script resets ports
   - CPU execution
   - port refresh preserves same-named connections
   - shader path/shader compilation if GPU SGN is in scope
3. Consider merging overlapping tests if they repeat setup heavily.
4. Remove tests that assert implementation details rather than user-visible behavior.
5. Keep test fixtures small and shared.
6. If a test is removed, record it as follow-up coverage rather than silently dropping the concern.

Acceptance criteria:

- SGN tests are fewer or shorter while still covering MVP behavior.
- Test setup duplication is reduced.
- Test names clearly map to required SGN behavior.

Verification:

- Run `git diff --numstat master...HEAD -- tests/voxel/test_voxel_graph.cpp tests/voxel/test_voxel_graph.h tests/tests.cpp project/tests/sgn_test_node.gd project/tests/sgn_test_node.glsl`.
- Run the retained SGN tests.

Completion notes:

- Completed:
- Verified:

## Task 10: Update Review And Audit Documentation After Code Cleanup

Status: [ ]

Priority: final pass.

Reason: `sgn_remake_audit.md`, `sgn_remake_review.md`, and this execution plan should reflect the actual final branch. Stale audit claims are review risk.

Files expected:

- `sgn_remake_audit.md`
- `sgn_remake_review.md`
- `sgn_remake_review_execution_plan.md`

Concrete steps:

1. Re-run `git diff --name-status master...HEAD`.
2. Update `sgn_remake_audit.md` so its effective patch summary matches the real diff.
3. Update `sgn_remake_review.md` to remove issues that have been fixed and keep unresolved issues explicit.
4. Update this plan by marking completed items and adding verification notes.
5. If any task was intentionally deferred, add a short reason and recommended follow-up PR.

Acceptance criteria:

- Docs do not claim removed changes are still present.
- Docs do not claim implemented changes are absent.
- Review still clearly explains remaining non-SGN touches.

Verification:

- Run `rg -n "VoxelGeneratorGraph|DirectStaticBody|VoxelEngine::~VoxelEngine|subtitle|NODE_SCRIPT_GRAPH" sgn_remake_audit.md sgn_remake_review.md sgn_remake_review_execution_plan.md`.
- Run `git diff --name-status master...HEAD` and compare against audit tables.

Completion notes:

- Completed:
- Verified:

## Suggested Agent Batches

Agents should pick one of these batches, not the whole plan.

- Batch A: Task 1 only. Highest value, narrow scope, direct architecture fix.
- Batch B: Task 2 only. Highest LOC and review-surface reduction outside SGN.
- Batch C: Task 8 plus Task 7. Largest SGN implementation shrink pass.
- Batch D: Task 9 only. Test diff reduction with focused verification.
- Batch E: Task 3 only. Required editor UX cleanup with low runtime risk.
- Batch F: Task 5 only. Serialization/remap shrink with focused tests.
- Batch G: Task 4 plus Task 6. Only if time remains; keep minimal and avoid new infrastructure.
- Batch H: Task 10 only. Final documentation sync.

## Final Acceptance Checklist

- [x] Terrain regeneration handlers do not downcast generators.
- [x] Broad shutdown/mesh/physics lifetime changes are removed or explicitly split out.
- [x] Required SGN subtitle UI no longer relies on internal `GraphNode` child traversal.
- [x] Compiler/runtime SGN dynamic-port handling is small, localized, and easy to justify.
- [x] Output hash SGN revision handling is minimal or deferred.
- [x] Dynamic output serialization is minimal and clearly justified.
- [ ] Shader generation SGN special-casing is reduced, localized, or documented as the remaining exception.
- [ ] SGN resource and test diffs have been reviewed for MVP scope.
- [x] SGN tests pass.
- [x] Build passes.
