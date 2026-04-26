#include "voxel_nav_region_3d.h"

#include "../util/godot/core/class_db.h"
#include "../util/godot/classes/node.h"
#include "../util/io/log.h"
#include "../util/string/format.h"
#include "variable_lod/voxel_lod_terrain.h"

namespace zylann::voxel {

namespace {

Array make_collision_subset(Array arrays, int vertex_count, int index_count, Vector3 vertex_offset) {
	if (arrays.size() != Mesh::ARRAY_MAX) {
		return Array();
	}

	const PackedVector3Array src_vertices = arrays[Mesh::ARRAY_VERTEX];
	const PackedInt32Array src_indices = arrays[Mesh::ARRAY_INDEX];
	if (vertex_count < 0 || index_count < 0) {
		vertex_count = src_vertices.size();
		index_count = src_indices.size();
	}
	if (vertex_count > src_vertices.size() || index_count > src_indices.size()) {
		return Array();
	}

	PackedVector3Array dst_vertices;
	dst_vertices.resize(vertex_count);
	{
		Vector3 *dst = dst_vertices.ptrw();
		const Vector3 *src = src_vertices.ptr();
		for (int i = 0; i < vertex_count; ++i) {
			dst[i] = src[i] + vertex_offset;
		}
	}

	PackedInt32Array dst_indices;
	dst_indices.resize(index_count);
	{
		int32_t *dst = dst_indices.ptrw();
		const int32_t *src = src_indices.ptr();
		for (int i = 0; i < index_count; ++i) {
			dst[i] = src[i];
		}
	}

	Array dst_arrays;
	dst_arrays.resize(Mesh::ARRAY_MAX);
	dst_arrays[Mesh::ARRAY_VERTEX] = dst_vertices;
	dst_arrays[Mesh::ARRAY_INDEX] = dst_indices;
	return dst_arrays;
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
	_terrain = resolve_terrain();
	if (_terrain == nullptr) {
		return false;
	}

	ensure_source_mesh_instance();

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

	int vertex_count = 0;
	int index_count = 0;
	Ref<ArrayMesh> mesh = create_source_mesh_from_voxel_block(vertex_count, index_count);
	if (mesh.is_null()) {
		_source_mesh_instance->set_mesh(Ref<Mesh>());
		return false;
	}
	_source_mesh_instance->set_mesh(mesh);

	return true;
}

void VoxelNavRegion3D::ensure_source_mesh_instance() {
	if (_source_mesh_instance != nullptr) {
		return;
	}

	_source_mesh_instance = memnew(MeshInstance3D);
	_source_mesh_instance->set_name("VoxelNavSourceMesh");
	_source_mesh_instance->set_visible(false);
	add_child(_source_mesh_instance);
}

Ref<ArrayMesh> VoxelNavRegion3D::create_source_mesh_from_voxel_block(int &out_vertex_count, int &out_index_count) const {
	out_vertex_count = 0;
	out_index_count = 0;

	ERR_FAIL_COND_V(_terrain == nullptr, Ref<ArrayMesh>());

	const int mesh_block_size = _terrain->get_mesh_block_size();
	const int region_size_blocks = 1 << _region_size_power;
	PackedVector3Array vertices;
	PackedInt32Array indices;

	for (int z = 0; z < region_size_blocks; ++z) {
		for (int y = 0; y < region_size_blocks; ++y) {
			for (int x = 0; x < region_size_blocks; ++x) {
				const Vector3i sub_block_position = _block_position + Vector3i(x, y, z);
				int collision_vertex_end = -1;
				int collision_index_end = -1;
				Array arrays = _terrain->generate_mesh_block_surface_for_navigation(
						sub_block_position, 0, collision_vertex_end, collision_index_end
				);
				if (arrays.size() == 0) {
					continue;
				}

				const Vector3 vertex_offset = Vector3(x, y, z) * mesh_block_size;
				arrays = make_collision_subset(arrays, collision_vertex_end, collision_index_end, vertex_offset);
				if (arrays.size() == 0) {
					continue;
				}

				const PackedVector3Array block_vertices = arrays[Mesh::ARRAY_VERTEX];
				const PackedInt32Array block_indices = arrays[Mesh::ARRAY_INDEX];
				if (block_vertices.size() == 0 || block_indices.size() == 0) {
					continue;
				}

				const int index_offset = vertices.size();
				for (int i = 0; i < block_vertices.size(); ++i) {
					vertices.push_back(block_vertices[i]);
				}
				for (int i = 0; i < block_indices.size(); ++i) {
					indices.push_back(index_offset + block_indices[i]);
				}
			}
		}
	}

	if (vertices.size() == 0 || indices.size() == 0) {
		return Ref<ArrayMesh>();
	}

	out_vertex_count = vertices.size();
	out_index_count = indices.size();

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
	if (_source_mesh_instance == nullptr || _source_mesh_instance->get_mesh().is_null()) {
		update_source_from_properties();
	}

	if (_source_mesh_instance == nullptr || _source_mesh_instance->get_mesh().is_null()) {
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

	Ref<NavigationMeshSourceGeometryData3D> source_geometry_data;
	source_geometry_data.instantiate();
	source_geometry_data->add_mesh(_source_mesh_instance->get_mesh(), Transform3D());
	NavigationServer3D::get_singleton()->bake_from_source_geometry_data(navigation_mesh, source_geometry_data);
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

void VoxelNavRegion3D::clear_navigation_mesh() {
	Ref<NavigationMesh> navigation_mesh = get_navigation_mesh();
	if (navigation_mesh.is_valid()) {
		navigation_mesh->clear();
		update_gizmos();
	}
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
	ClassDB::bind_method(D_METHOD("clear_navigation_mesh"), &Self::clear_navigation_mesh);
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
