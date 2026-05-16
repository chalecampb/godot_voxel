# ScriptGraphNode Remake Architecture Review

Review target: current `sgn_remake_noclip` branch compared against module `master`.

This review reflects the cleanup work completed after the initial architecture review. The earlier blockers around broad lifetime fixes, terrain downcasts, fragile subtitle handling, oversized shader-generator SGN behavior, and dynamic-port plumbing have been reduced or documented.

## Summary

The branch is now scoped much more tightly around ScriptGraphNode (SGN). The remaining non-SGN touches are explainable as local graph/editor infrastructure needed for SGN dynamic ports, reload workflow, shader inclusion, or tests.

The branch now satisfies the main review constraints:

- Terrain regeneration no longer downcasts generators to `VoxelGeneratorGraph`.
- Broad shutdown, mesh block, physics body, and task-runner changes have been removed from the effective diff.
- SGN subtitle UI no longer inspects or reparents internal `GraphNode` titlebar children.
- Runtime dynamic-port handling remains a direct SGN special case only where compiled operations need dynamic input/output counts.
- SGN GLSL parsing and source rewriting now live on `VoxelGraphScriptNode`, so the upper shader generator only requests SGN shader source and emits the graph call.
- Tests were reviewed for pertinence and intentionally retained because they cover the behavior most likely to regress during SGN review cleanup.

## Current Diff Shape

The effective branch diff is SGN-focused. It touches:

- SGN resource contract and class registration.
- Graph node registration, dynamic layout refresh, dynamic port serialization/remap, copy/load preservation, and runtime dynamic count handling.
- SGN shader source integration, with GLSL parsing and namespacing owned by `VoxelGraphScriptNode`.
- Editor graph/inspector workflow for SGN script assignment, reload, parameters, validation, subtitle, and layout refresh.
- `VoxelGenerator::request_regeneration()` plus `VoxelGeneratorGraph::request_regeneration()` so generators prepare themselves before signaling terrain consumers.
- Small terrain signal handlers that restart terrain generically after generator regeneration is requested.
- Cache-replacing resource reload helper used by explicit SGN reload actions.
- Documentation and SGN tests/fixtures.

The effective diff no longer includes the earlier unrelated files:

- `engine/voxel_engine.cpp`
- `util/godot/direct_static_body.*`
- `terrain/voxel_mesh_block.*`
- `terrain/voxel_mesh_map.h`
- fixed/variable LOD mesh block teardown files

Those reliability changes should remain separate PR candidates if still needed.

## Architecture Review

### Call Down / Signal Up

The current regeneration flow is aligned with the intended direction:

1. The editor calls down into the edited generator with `generator->request_regeneration()`.
2. `VoxelGeneratorGraph::request_regeneration()` prepares graph state by compiling before signaling.
3. The generator emits `regeneration_requested` only after preparation succeeds.
4. Terrain consumers receive the signal and restart their own work without checking the concrete generator type.

This keeps generator behavior in the generator and terrain behavior in terrain.

### Upward Hierarchy Queries

The SGN path no longer relies on editor-side traversal to find terrain consumers. Consumers subscribe to generator signals instead. Existing unrelated parent checks outside the SGN path are not part of this branch review.

### Child-Type Introspection

The clearest child-type violation from the original review, terrain checking `VoxelGeneratorGraph`, has been fixed.

Some direct SGN checks remain in graph/editor integration. They are now narrower:

- `VoxelGraphFunction::get_node_subtitle()` returns the SGN script basename for graph UI.
- Compiler/runtime paths encode and decode dynamic SGN input/output counts.
- Graph serialization/remap preserves SGN dynamic output names across reload/copy.
- The shader generator detects `NODE_SCRIPT_GRAPH` only to ask the script node for shader source and emit a call.

These are acceptable for this PR because SGN is the first node with dynamic runtime output counts and a script-backed shader source contract. A future generic dynamic-node API could reduce these checks, but adding that framework now would grow the PR.

### Editor Subtitle

The subtitle feature is now implemented through controls owned by `VoxelGraphEditorNode`. It no longer recursively searches `GraphNode` children or reparents Godot's internal title label. This resolves the fragility concern while preserving the required UX signal: users can identify which script backs an SGN node on the graph canvas.

### Shader Generation

The upper shader generator no longer owns SGN GLSL parsing, namespacing, uniform substitution, or entry point validation. `VoxelGraphScriptNode::get_shader_source()` now returns the generated function name and source code needed by the graph shader generator.

Remaining shader-generator SGN code is limited to:

- fetching the SGN resource parameter,
- reporting a missing-resource error,
- registering the returned library code,
- emitting a call using graph input/output variables.

This is a reasonable boundary for the current SGN implementation.

### Dynamic Ports And Serialization

Dynamic output serialization and name-based remapping are still part of the graph layer. They are necessary for SGN because script changes can reorder or rename ports while saved connections are numeric. The implementation is now documented and localized, and tests cover refresh plus copy/load preservation.

### Type Usage

`std::string` remains in SGN GLSL parsing/rewrite paths where `std::regex` is used. It is no longer spread through the upper shader generator. Voxel-facing API boundaries return `String` or `StdString`.

## Test Coverage

The SGN tests are intentionally retained. They cover:

- script contract loading and dynamic graph layout names,
- clearing scripts and port reset,
- editor-visible shader path property,
- shader compilation, namespacing, uniform constant replacement, and call emission,
- SGN resource API and parameter value path,
- CPU runtime execution,
- debug preview compilation avoiding `changed` signal recursion,
- dynamic port refresh preserving same-named connections,
- copy/load preserving SGN connections by dynamic port name.

This is more than a minimal smoke suite, but it is pertinent coverage for the exact review risks this branch has been reducing.

## Remaining Follow-Up Candidates

These are not blockers for the SGN PR, but they are reasonable future cleanups:

- Introduce a generic graph dynamic-node policy if more node types need dynamic runtime output counts.
- Move subtitle/hash behavior behind node metadata callbacks if more node types need similar behavior.
- Consider a separate reliability PR for the previously removed shutdown/task/GPU/mesh lifetime fixes if those failures are still reproducible.
- Consider consolidating SGN GLSL validation and shader source generation parsing so both paths share more extracted helpers without broad parser rewrites.

## Verdict

The current branch is suitable for SGN-focused review. The remaining SGN special cases are localized, covered by tests, and tied to the script-backed dynamic-port contract rather than broad engine behavior.
