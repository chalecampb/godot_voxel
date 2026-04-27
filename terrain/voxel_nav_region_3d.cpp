#include "voxel_nav_region_3d.h"

#include "../util/godot/classes/time.h"
#include "../util/godot/core/class_db.h"
#include "../util/godot/classes/node.h"
#include "../util/io/log.h"
#include "../util/string/format.h"
#include "variable_lod/voxel_lod_terrain.h"

namespace zylann::voxel {

namespace {

inline uint64_t get_ticks_msec() {
	return Time::get_singleton()->get_ticks_msec();
}

} // namespace

VoxelNavRegion3D::VoxelNavRegion3D() {
	set_name("VoxelNavRegion3D");
}

void VoxelNavRegion3D::set_terrain_path(NodePath path) {
	_terrain_path = path;
	_terrain = nullptr;
	update_source_from_properties();
}

NodePath VoxelNavRegion3D::get_terrain_path() const {
	return _terrain_path;
}

void VoxelNavRegion3D::set_block_position(Vector3i block_position) {
	_block_position = block_position;
	update_source_from_properties();
}

void VoxelNavRegion3D::set_region_size_power(int power) {
	ERR_FAIL_COND(power < 0);
	ERR_FAIL_COND(power > 8);
	if (_region_size_power == power) {
		return;
	}
	_region_size_power = power;
	update_source_from_properties();
}

int VoxelNavRegion3D::get_region_size_power() const {
	return _region_size_power;
}

bool VoxelNavRegion3D::setup_from_voxel_block(
		VoxelLodTerrain *terrain,
		Vector3i block_position,
		int region_size_power,
		Ref<VoxelNavMeshSettings> settings,
		Transform3D region_transform
) {
	ERR_FAIL_COND_V(terrain == nullptr, false);

	_terrain = terrain;
	_block_position = block_position;
	_region_size_power = region_size_power;
	_settings = settings;
	if (is_inside_tree() && terrain->is_inside_tree()) {
		_terrain_path = get_path_to(terrain);
	}

	set_transform(region_transform);
	return update_source_from_properties();
}

void VoxelNavRegion3D::set_nav_mesh_settings(Ref<VoxelNavMeshSettings> settings) {
	_settings = settings;
	update_source_from_properties();
}

Ref<VoxelNavMeshSettings> VoxelNavRegion3D::get_nav_mesh_settings() const {
	return _settings;
}

VoxelLodTerrain *VoxelNavRegion3D::resolve_terrain() const {
	if (_terrain != nullptr) {
		return _terrain;
	}
	if (_terrain_path.is_empty() || !is_inside_tree()) {
		return nullptr;
	}
	if (!has_node(_terrain_path)) {
		return nullptr;
	}
	return zylann::godot::get_node_typed<VoxelLodTerrain>(*this, _terrain_path);
}

bool VoxelNavRegion3D::update_source_from_properties() {
	_source_mesh = Ref<ArrayMesh>();
	_terrain = resolve_terrain();
	if (_terrain == nullptr) {
		return false;
	}

	Ref<NavigationMesh> navigation_mesh = get_navigation_mesh();
	if (navigation_mesh.is_null()) {
		if (_settings.is_valid()) {
			navigation_mesh = _settings->instantiate_navigation_mesh();
		} else {
			navigation_mesh.instantiate();
		}
	}
	configure_navigation_mesh_bounds(navigation_mesh);
	set_navigation_mesh(navigation_mesh);
	return true;
}

Ref<ArrayMesh> VoxelNavRegion3D::create_source_mesh_from_lod0_collision() const {
	ERR_FAIL_COND_V(_terrain == nullptr, Ref<ArrayMesh>());

	const int region_size_blocks = 1 << _region_size_power;
	PackedVector3Array vertices;
	PackedInt32Array indices;
	if (!_terrain->generate_lod0_collision_mesh_for_navigation_region(
				_block_position, region_size_blocks, vertices, indices
	)) {
		return Ref<ArrayMesh>();
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_INDEX] = indices;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	return mesh;
}

void VoxelNavRegion3D::configure_navigation_mesh_bounds(Ref<NavigationMesh> navigation_mesh) const {
	if (navigation_mesh.is_null() || _terrain == nullptr) {
		return;
	}

	const float block_size = _terrain->get_mesh_block_size() * (1 << _region_size_power);
	navigation_mesh->set_border_size(0.f);
	navigation_mesh->set_filter_baking_aabb(AABB(Vector3(), Vector3(block_size, block_size, block_size)));
	navigation_mesh->set_filter_baking_aabb_offset(Vector3());
}

void VoxelNavRegion3D::bake_navigation_mesh() {
	_last_source_mesh_time_msec = 0;
	_last_source_geometry_time_msec = 0;
	_last_navigation_bake_time_msec = 0;

	_source_mesh = Ref<ArrayMesh>();
	_terrain = resolve_terrain();
	const uint64_t source_mesh_time_before = get_ticks_msec();
	_source_mesh = create_source_mesh_from_lod0_collision();
	_last_source_mesh_time_msec = get_ticks_msec() - source_mesh_time_before;

	if (_source_mesh.is_null()) {
		zylann::print_line(
				format("VoxelNavRegion3D: skipped bake for block ({}, {}, {}), no source mesh",
					   _block_position.x,
					   _block_position.y,
					   _block_position.z)
		);
		return;
	}

	bake_navigation_mesh_from_current_source();
}

void VoxelNavRegion3D::bake_navigation_mesh_from_current_source() {
	_last_source_geometry_time_msec = 0;
	_last_navigation_bake_time_msec = 0;

	if (_source_mesh.is_null()) {
		zylann::print_line(
				format("VoxelNavRegion3D: skipped bake for block ({}, {}, {}), no source mesh",
					   _block_position.x,
					   _block_position.y,
					   _block_position.z)
		);
		return;
	}

	Ref<NavigationMesh> navigation_mesh = get_navigation_mesh();
	if (navigation_mesh.is_null()) {
		if (_settings.is_valid()) {
			navigation_mesh = _settings->instantiate_navigation_mesh();
		} else {
			navigation_mesh.instantiate();
		}
		configure_navigation_mesh_bounds(navigation_mesh);
		set_navigation_mesh(navigation_mesh);
	}

	const uint64_t source_geometry_time_before = get_ticks_msec();
	Ref<NavigationMeshSourceGeometryData3D> source_geometry_data;
	source_geometry_data.instantiate();
	source_geometry_data->add_mesh(_source_mesh, Transform3D());
	_last_source_geometry_time_msec = get_ticks_msec() - source_geometry_time_before;

	const uint64_t navigation_bake_time_before = get_ticks_msec();
	NavigationServer3D::get_singleton()->bake_from_source_geometry_data(navigation_mesh, source_geometry_data);
	_last_navigation_bake_time_msec = get_ticks_msec() - navigation_bake_time_before;
	update_gizmos();
	if (navigation_mesh->get_polygon_count() == 0) {
		zylann::print_line(
				format("VoxelNavRegion3D: baked block ({}, {}, {}) with 0 polygons",
					   _block_position.x,
					   _block_position.y,
					   _block_position.z)
		);
	}
	emit_signal("bake_finished");
}

void VoxelNavRegion3D::set_source_mesh_from_collision_arrays(
		const PackedVector3Array &vertices,
		const PackedInt32Array &indices,
		uint64_t source_mesh_time_msec
) {
	_source_mesh = Ref<ArrayMesh>();
	_last_source_mesh_time_msec = source_mesh_time_msec;
	_last_source_geometry_time_msec = 0;
	_last_navigation_bake_time_msec = 0;

	if (vertices.size() < 3 || indices.size() < 3) {
		return;
	}

	const uint64_t time_before = get_ticks_msec();
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_INDEX] = indices;
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	_source_mesh = mesh;
	_last_source_mesh_time_msec += get_ticks_msec() - time_before;
}

void VoxelNavRegion3D::clear_source_mesh() {
	_source_mesh = Ref<ArrayMesh>();
	_last_source_mesh_time_msec = 0;
	_last_source_geometry_time_msec = 0;
	_last_navigation_bake_time_msec = 0;
}

void VoxelNavRegion3D::clear_navigation_mesh() {
	Ref<NavigationMesh> navigation_mesh = get_navigation_mesh();
	if (navigation_mesh.is_valid()) {
		navigation_mesh->clear();
		update_gizmos();
	}
}

bool VoxelNavRegion3D::has_source_mesh() const {
	return _source_mesh.is_valid();
}

uint64_t VoxelNavRegion3D::get_last_source_mesh_time_msec() const {
	return _last_source_mesh_time_msec;
}

uint64_t VoxelNavRegion3D::get_last_source_geometry_time_msec() const {
	return _last_source_geometry_time_msec;
}

uint64_t VoxelNavRegion3D::get_last_navigation_bake_time_msec() const {
	return _last_navigation_bake_time_msec;
}

Vector3i VoxelNavRegion3D::get_block_position() const {
	return _block_position;
}

VoxelLodTerrain *VoxelNavRegion3D::get_terrain() const {
	return _terrain;
}

void VoxelNavRegion3D::_notification(int what) {
	NavigationRegion3D::_notification(what);

	switch (what) {
		case NOTIFICATION_ENTER_TREE:
			update_source_from_properties();
			break;
	}
}

void VoxelNavRegion3D::_bind_methods() {
	using Self = VoxelNavRegion3D;

	ClassDB::bind_method(D_METHOD("bake_navigation_mesh"), &Self::bake_navigation_mesh);
	ClassDB::bind_method(D_METHOD("bake_navigation_mesh_from_current_source"), &Self::bake_navigation_mesh_from_current_source);
	ClassDB::bind_method(D_METHOD("clear_navigation_mesh"), &Self::clear_navigation_mesh);
	ClassDB::bind_method(D_METHOD("has_source_mesh"), &Self::has_source_mesh);
	ClassDB::bind_method(D_METHOD("get_last_source_mesh_time_msec"), &Self::get_last_source_mesh_time_msec);
	ClassDB::bind_method(D_METHOD("get_last_source_geometry_time_msec"), &Self::get_last_source_geometry_time_msec);
	ClassDB::bind_method(D_METHOD("get_last_navigation_bake_time_msec"), &Self::get_last_navigation_bake_time_msec);
	ClassDB::bind_method(D_METHOD("set_terrain_path", "path"), &Self::set_terrain_path);
	ClassDB::bind_method(D_METHOD("get_terrain_path"), &Self::get_terrain_path);
	ClassDB::bind_method(D_METHOD("set_block_position", "block_position"), &Self::set_block_position);
	ClassDB::bind_method(D_METHOD("get_block_position"), &Self::get_block_position);
	ClassDB::bind_method(D_METHOD("set_region_size_power", "power"), &Self::set_region_size_power);
	ClassDB::bind_method(D_METHOD("get_region_size_power"), &Self::get_region_size_power);
	ClassDB::bind_method(D_METHOD("set_nav_mesh_settings", "settings"), &Self::set_nav_mesh_settings);
	ClassDB::bind_method(D_METHOD("get_nav_mesh_settings"), &Self::get_nav_mesh_settings);
	ClassDB::bind_method(D_METHOD("get_terrain"), &Self::get_terrain);

	ADD_PROPERTY(
			PropertyInfo(Variant::NODE_PATH, "terrain_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "VoxelLodTerrain"),
			"set_terrain_path",
			"get_terrain_path"
	);
	ADD_PROPERTY(
			PropertyInfo(Variant::VECTOR3I, "block_position"),
			"set_block_position",
			"get_block_position"
	);
	ADD_PROPERTY(
			PropertyInfo(Variant::INT, "region_size_power", PROPERTY_HINT_RANGE, "0,8,1"),
			"set_region_size_power",
			"get_region_size_power"
	);
	ADD_PROPERTY(
			PropertyInfo(Variant::OBJECT, "nav_mesh_settings", PROPERTY_HINT_RESOURCE_TYPE, "VoxelNavMeshSettings"),
			"set_nav_mesh_settings",
			"get_nav_mesh_settings"
	);
}

} // namespace zylann::voxel
