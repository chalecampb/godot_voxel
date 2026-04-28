Issues Summary 
    - Issues were noted during use

VoxelNavRegion3D
    - [Resolved] VoxelNavRegion3D does not bake nav mesh when added standalone with a TerrainPath, or with Terrain as a child
        - Block Position and Transform set to 0,0,0
        - Only works if placed by a VoxelNavManager3D
        - Resolution: standalone regions now resolve either `terrain_path` or a child `VoxelLodTerrain`, and derive `block_position` from the region transform relative to the selected terrain before baking.

    - [Resolved] There is a third UI box - bake navigationmesh, clear navigationmesh, then VoxelNavRegion3D -> bake and clear
         - We should only have Bake NavigationMesh, and Clear NavigationMesh, no additional 
         - That said, VoxelNavRegion3D -> Bake NavigationMesh works perfectly, just the UI is wrong. 
         - Resolution: the voxel nav editor plugin no longer handles `VoxelNavRegion3D`, so the extra `VoxelNavRegion3D` menu is not added for region selection.

VoxelNavManager3D
    - [Resolved] Bake UI does not match VoxelNavRegion3D
        - Expect Bake Navigation Regions  or Clear Navigation Regions, which should start the baking process
        - Bake Navigation Regions should set up the regions and bake them. Clear should remove them. 
        - Resolution: the manager editor UI now exposes direct flat toolbar buttons for `Bake Navigation Regions` and `Clear Navigation Regions`, matching the native `NavigationRegion3D` bake/clear button style and icons; bake rebuilds regions then bakes, and clear removes generated regions.
    - [Resolved] Regions which were generated adjacent, are unreachable when we try to navigate to them. 
        - There is something wrong with the inherited Region behavior where regions are supposed to connect when adjacent. Check settings, limits, etc. 
        - It looks like, to me, the regions still have the gap issue. The gap issue is critical. 
        - Two adjacent regions with calculated meshes must have the mesh go all the way to the edge of the region. This may mean that the region must be a slight margin larger than intended.
        - For the nav server to merge regions and treat them as contiguous, they must share edge vertices - overlapping. 
        - Resolution: voxel nav regions now enable edge connections, and nav source generation includes a one-block source border around each region so adjacent bakes have overlapping border geometry while retaining the configured region bake bounds.
        
    - [Resolved] There is STILL a slight gap at the edge of the regions. 
        - It's around .785 units total - so each region is around .785/2 from the edge. 
        - This is enough to break nav because the regions must have edge overlapping vertices to be connected. 
        - Please find the reason for the slight gap, and confirm we are generating right up to exactly the navigation region bounds. 
        - Resolution: source geometry includes a one-block border, and the bake filter AABB is expanded on X/Z by `ceil(agent_radius / cell_size) * cell_size` while `NavigationMesh.border_size` is set to that same value. Godot cuts the final baked surface inward by `border_size`, so the final navmesh edge lands exactly on the configured region bounds instead of being inset or overlapping. `edge_max_error` is capped to `1.0` for tile-aligned border edges.

    - [Resolved] Manager3D's clear button doesn't clear the nav meshes or regions.
        - Resolution: generated regions are now marked when created, and clear removes both tracked generated regions and direct generated child regions that may remain after editor reloads or stale manager state. Each region's navigation mesh is cleared before the region node is removed.

    - [Resolved] Adjacent regions only connect after moving a region in the editor.
        - The baked region bounds are aligned, but the edge-connection indicator is drawn only after `NavigationServer3D` emits `map_changed` from a completed map iteration. With async region/map iterations, a single forced update can start the region and map rebuilds without completing the map iteration. Moving a region gives the server later sync passes, which is why connections appear afterward.
        - Resolution: after baking or clearing a voxel navigation mesh, `VoxelNavRegion3D` now pushes the current global transform and mesh to `NavigationServer3D`, temporarily runs the region/map iteration synchronously for that update, then restores the previous async settings. No manual navigation links or persistent connection objects are created.
