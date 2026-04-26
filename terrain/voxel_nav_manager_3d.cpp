#include "voxel_nav_manager_3d.h"

#include "../util/godot/core/class_db.h"
#include "../util/io/log.h"
#include "../util/math/conv.h"
#include "../util/math/box3i.h"
#include "../util/string/format.h"
#include "variable_lod/voxel_lod_terrain.h"
#include "voxel_nav_region_3d.h"

namespace zylann::voxel {

namespace {

constexpr uint64_t MAX_REGIONS_TO_CREATE = 65536;

} // namespace

VoxelNavManager3D::VoxelNavManager3D() {}

void VoxelNavManager3D::set_nav_mesh_settings(Ref<VoxelNavMeshSettings> settings) {
	_nav_mesh_settings = settings;
}

Ref<VoxelNavMeshSettings> VoxelNavManager3D::get_nav_mesh_settings() const {
	return _nav_mesh_settings;
}

void VoxelNavManager3D::set_bake_on_ready(bool enabled) {
	_bake_on_ready = enabled;
}

bool VoxelNavManager3D::get_bake_on_ready() const {
	return _bake_on_ready;
}

void VoxelNavManager3D::set_navigation_layers(uint32_t navigation_layers) {
	if (_navigation_layers == navigation_layers) {
		return;
	}

	_navigation_layers = navigation_layers;

	for (VoxelNavRegion3D *region : _regions) {
		if (region != nullptr) {
			region->set_navigation_layers(_navigation_layers);
		}
	}
}

uint32_t VoxelNavManager3D::get_navigation_layers() const {
	return _navigation_layers;
}

void VoxelNavManager3D::set_navigation_layer_value(int layer_number, bool value) {
	ERR_FAIL_COND_MSG(layer_number < 1, "Navigation layer number must be between 1 and 32 inclusive.");
	ERR_FAIL_COND_MSG(layer_number > 32, "Navigation layer number must be between 1 and 32 inclusive.");

	uint32_t navigation_layers = get_navigation_layers();

	if (value) {
		navigation_layers |= 1 << (layer_number - 1);
	} else {
		navigation_layers &= ~(1 << (layer_number - 1));
	}

	set_navigation_layers(navigation_layers);
}

bool VoxelNavManager3D::get_navigation_layer_value(int layer_number) const {
	ERR_FAIL_COND_V_MSG(layer_number < 1, false, "Navigation layer number must be between 1 and 32 inclusive.");
	ERR_FAIL_COND_V_MSG(layer_number > 32, false, "Navigation layer number must be between 1 and 32 inclusive.");

	return get_navigation_layers() & (1 << (layer_number - 1));
}

void VoxelNavManager3D::set_region_size_power(int power) {
	ERR_FAIL_COND(power < 0);
	ERR_FAIL_COND(power > 8);
	_region_size_power = power;
}

int VoxelNavManager3D::get_region_size_power() const {
	return _region_size_power;
}

void VoxelNavManager3D::rebuild_regions() {
	clear_regions();

	StdVector<VoxelLodTerrain *> terrains;
	collect_voxel_lod_terrains(this, terrains);
	zylann::print_line(format("VoxelNavManager3D: found {} VoxelLodTerrain node(s)", terrains.size()));

	for (VoxelLodTerrain *terrain : terrains) {
		const int mesh_block_size = terrain->get_mesh_block_size();
		const int region_size_blocks = 1 << _region_size_power;
		const int region_size_voxels = mesh_block_size * region_size_blocks;
		const Box3i voxel_bounds = terrain->get_voxel_bounds();
		const Vector3i region_min = math::floordiv(voxel_bounds.position, region_size_voxels);
		const Vector3i region_max = math::ceildiv(voxel_bounds.position + voxel_bounds.size, region_size_voxels);
		const Box3i region_bounds = Box3i::from_min_max(region_min, region_max);
		const uint64_t region_count = Vector3iUtil::get_volume_u64(region_bounds.size);

		zylann::print_line(
				format("VoxelNavManager3D: terrain voxel bounds pos=({}, {}, {}) size=({}, {}, {}), mesh_block_size={}, region_size_power={}, region_size_voxels={}, region grid pos=({}, {}, {}) size=({}, {}, {})",
					   voxel_bounds.position.x,
					   voxel_bounds.position.y,
					   voxel_bounds.position.z,
					   voxel_bounds.size.x,
					   voxel_bounds.size.y,
					   voxel_bounds.size.z,
					   mesh_block_size,
					   _region_size_power,
					   region_size_voxels,
					   region_bounds.position.x,
					   region_bounds.position.y,
					   region_bounds.position.z,
					   region_bounds.size.x,
					   region_bounds.size.y,
					   region_bounds.size.z)
		);

		if (region_count > MAX_REGIONS_TO_CREATE) {
			ZN_PRINT_WARNING(format(
					"VoxelNavManager3D: skipped terrain because voxel_bounds would create {} regions. Set tighter "
					"voxel_bounds, increase region_size_power, or increase the implementation cap.",
					region_count
			));
			continue;
		}

		int skipped_empty_region_count = 0;
		region_bounds.for_each_cell([this, terrain, region_size_blocks, &skipped_empty_region_count](Vector3i region_position) {
			const Vector3i block_position = region_position * region_size_blocks;
			VoxelNavRegion3D *region = create_region(*terrain, block_position);
			if (region != nullptr) {
				_regions.push_back(region);
			} else {
				++skipped_empty_region_count;
			}
		});
		if (skipped_empty_region_count > 0) {
			zylann::print_line(
					format("VoxelNavManager3D: skipped {} empty region(s) with no mesh data", skipped_empty_region_count)
			);
		}
	}
	zylann::print_line(format("VoxelNavManager3D: rebuilt {} navigation region(s)", _regions.size()));
}

void VoxelNavManager3D::bake_navigation_meshes() {
	if (_regions.size() == 0) {
		rebuild_regions();
	}

	int baked_count = 0;
	int polygon_count = 0;
	for (VoxelNavRegion3D *region : _regions) {
		if (region != nullptr && region->is_inside_tree()) {
			region->bake_navigation_mesh();
			Ref<NavigationMesh> navigation_mesh = region->get_navigation_mesh();
			if (navigation_mesh.is_valid()) {
				const int region_polygon_count = navigation_mesh->get_polygon_count();
				if (region_polygon_count > 0) {
					++baked_count;
					polygon_count += region_polygon_count;
				}
			}
		}
	}
	zylann::print_line(
			format("VoxelNavManager3D: baked {} / {} navigation mesh(es), {} polygon(s)",
				   baked_count,
				   _regions.size(),
				   polygon_count)
	);
}

void VoxelNavManager3D::clear_navigation_meshes() {
	for (VoxelNavRegion3D *region : _regions) {
		if (region != nullptr && region->is_inside_tree()) {
			region->clear_navigation_mesh();
		}
	}
}

void VoxelNavManager3D::clear_regions() {
	for (VoxelNavRegion3D *region : _regions) {
		if (region == nullptr) {
			continue;
		}
		if (region->get_parent() == this) {
			remove_child(region);
		}
		memdelete(region);
	}
	_regions.clear();
}

int VoxelNavManager3D::get_region_count() const {
	return _regions.size();
}

void VoxelNavManager3D::collect_voxel_lod_terrains(Node *node, StdVector<VoxelLodTerrain *> &out_terrains) const {
	for (int i = 0; i < node->get_child_count(); ++i) {
		Node *child = node->get_child(i);
		VoxelLodTerrain *terrain = Object::cast_to<VoxelLodTerrain>(child);
		if (terrain != nullptr) {
			out_terrains.push_back(terrain);
			continue;
		}
		if (Object::cast_to<VoxelNavRegion3D>(child) == nullptr) {
			collect_voxel_lod_terrains(child, out_terrains);
		}
	}
}

VoxelNavRegion3D *VoxelNavManager3D::create_region(VoxelLodTerrain &terrain, Vector3i block_position) {
	VoxelNavRegion3D *region = memnew(VoxelNavRegion3D);
	region->set_name(
			String("VoxelNavRegion_{0}_{1}_{2}").format(varray(block_position.x, block_position.y, block_position.z))
	);
	add_child(region);
	if (get_owner() != nullptr) {
		region->set_owner(get_owner());
	}
	region->set_navigation_layers(_navigation_layers);

	const float block_size = terrain.get_mesh_block_size();
	const Transform3D block_transform(Basis(), to_vec3(block_position) * block_size);
	const Transform3D region_transform =
			get_global_transform().affine_inverse() * terrain.get_global_transform() * block_transform;
	if (!region->setup_from_voxel_block(
				&terrain, block_position, _region_size_power, _nav_mesh_settings, region_transform
		)) {
		remove_child(region);
		memdelete(region);
		return nullptr;
	}
	return region;
}

void VoxelNavManager3D::_notification(int what) {
	switch (what) {
		case NOTIFICATION_READY:
			if (_bake_on_ready) {
				rebuild_regions();
				bake_navigation_meshes();
			}
			break;
	}
}

void VoxelNavManager3D::_bind_methods() {
	using Self = VoxelNavManager3D;

	ClassDB::bind_method(D_METHOD("set_nav_mesh_settings", "settings"), &Self::set_nav_mesh_settings);
	ClassDB::bind_method(D_METHOD("get_nav_mesh_settings"), &Self::get_nav_mesh_settings);

	ClassDB::bind_method(D_METHOD("set_bake_on_ready", "enabled"), &Self::set_bake_on_ready);
	ClassDB::bind_method(D_METHOD("get_bake_on_ready"), &Self::get_bake_on_ready);

	ClassDB::bind_method(D_METHOD("set_navigation_layers", "navigation_layers"), &Self::set_navigation_layers);
	ClassDB::bind_method(D_METHOD("get_navigation_layers"), &Self::get_navigation_layers);
	ClassDB::bind_method(D_METHOD("set_navigation_layer_value", "layer_number", "value"), &Self::set_navigation_layer_value);
	ClassDB::bind_method(D_METHOD("get_navigation_layer_value", "layer_number"), &Self::get_navigation_layer_value);

	ClassDB::bind_method(D_METHOD("set_region_size_power", "power"), &Self::set_region_size_power);
	ClassDB::bind_method(D_METHOD("get_region_size_power"), &Self::get_region_size_power);

	ClassDB::bind_method(D_METHOD("rebuild_regions"), &Self::rebuild_regions);
	ClassDB::bind_method(D_METHOD("bake_navigation_meshes"), &Self::bake_navigation_meshes);
	ClassDB::bind_method(D_METHOD("clear_navigation_meshes"), &Self::clear_navigation_meshes);
	ClassDB::bind_method(D_METHOD("clear_regions"), &Self::clear_regions);
	ClassDB::bind_method(D_METHOD("get_region_count"), &Self::get_region_count);

	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "nav_mesh_settings", PROPERTY_HINT_RESOURCE_TYPE, "VoxelNavMeshSettings"),
			"set_nav_mesh_settings",
			"get_nav_mesh_settings"
	);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "bake_on_ready"), "set_bake_on_ready", "get_bake_on_ready");
	ADD_PROPERTY(
			PropertyInfo(Variant::INT, "navigation_layers", PROPERTY_HINT_LAYERS_3D_NAVIGATION),
			"set_navigation_layers",
			"get_navigation_layers"
	);
	ADD_PROPERTY(
			PropertyInfo(Variant::INT, "region_size_power", PROPERTY_HINT_RANGE, "0,8,1"),
			"set_region_size_power",
			"get_region_size_power"
	);
}

} // namespace zylann::voxel
