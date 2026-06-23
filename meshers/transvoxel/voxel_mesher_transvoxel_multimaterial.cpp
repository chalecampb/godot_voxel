#include "voxel_mesher_transvoxel_multimaterial.h"
#include "../../constants/voxel_constants.h"
#include "../../storage/voxel_buffer.h"
#include "../../util/containers/container_funcs.h"
#include "../../util/godot/classes/rendering_server.h"
#include "../../util/godot/core/class_db.h"
#include "../../util/godot/core/packed_arrays.h"
#include "../../util/godot/core/string.h"
#include "../../util/math/conv.h"
#include "../../util/math/funcs.h"
#include "../../util/profiling.h"

#include <array>

using namespace zylann::godot;

namespace zylann::voxel {

namespace {

static const float HIDDEN_SURFACE_SDF_THRESHOLD = 0.25f;

struct MaterialSet {
	std::array<bool, 256> used;

	MaterialSet() {
		used.fill(false);
	}
};

float sample_sdf_trilinear(const VoxelBuffer &voxels, Vector3f pos) {
	const Vector3i size = voxels.get_size();

	pos.x = math::clamp(pos.x, 0.f, static_cast<float>(size.x - 1));
	pos.y = math::clamp(pos.y, 0.f, static_cast<float>(size.y - 1));
	pos.z = math::clamp(pos.z, 0.f, static_cast<float>(size.z - 1));

	const int x0 = math::clamp(static_cast<int>(Math::floor(pos.x)), 0, size.x - 1);
	const int y0 = math::clamp(static_cast<int>(Math::floor(pos.y)), 0, size.y - 1);
	const int z0 = math::clamp(static_cast<int>(Math::floor(pos.z)), 0, size.z - 1);
	const int x1 = math::min(x0 + 1, size.x - 1);
	const int y1 = math::min(y0 + 1, size.y - 1);
	const int z1 = math::min(z0 + 1, size.z - 1);

	const float tx = pos.x - static_cast<float>(x0);
	const float ty = pos.y - static_cast<float>(y0);
	const float tz = pos.z - static_cast<float>(z0);

	const float v000 = voxels.get_voxel_f(x0, y0, z0, VoxelBuffer::CHANNEL_SDF);
	const float v100 = voxels.get_voxel_f(x1, y0, z0, VoxelBuffer::CHANNEL_SDF);
	const float v010 = voxels.get_voxel_f(x0, y1, z0, VoxelBuffer::CHANNEL_SDF);
	const float v110 = voxels.get_voxel_f(x1, y1, z0, VoxelBuffer::CHANNEL_SDF);
	const float v001 = voxels.get_voxel_f(x0, y0, z1, VoxelBuffer::CHANNEL_SDF);
	const float v101 = voxels.get_voxel_f(x1, y0, z1, VoxelBuffer::CHANNEL_SDF);
	const float v011 = voxels.get_voxel_f(x0, y1, z1, VoxelBuffer::CHANNEL_SDF);
	const float v111 = voxels.get_voxel_f(x1, y1, z1, VoxelBuffer::CHANNEL_SDF);

	const float v00 = Math::lerp(v000, v100, tx);
	const float v10 = Math::lerp(v010, v110, tx);
	const float v01 = Math::lerp(v001, v101, tx);
	const float v11 = Math::lerp(v011, v111, tx);
	const float v0 = Math::lerp(v00, v10, ty);
	const float v1 = Math::lerp(v01, v11, ty);
	return Math::lerp(v0, v1, tz);
}

Vector3f to_voxel_sample_position(const Vector3f mesh_position, const uint8_t lod_index) {
	const float inv_lod_scale = 1.f / static_cast<float>(1 << lod_index);
	return mesh_position * inv_lod_scale + Vector3f(
												  transvoxel::MIN_PADDING,
												  transvoxel::MIN_PADDING,
												  transvoxel::MIN_PADDING
										  );
}

bool is_triangle_on_global_surface(
		const VoxelBuffer &global_voxels,
		const Vector3f p0,
		const Vector3f p1,
		const Vector3f p2,
		const uint8_t lod_index
) {
	const float s0 = sample_sdf_trilinear(global_voxels, to_voxel_sample_position(p0, lod_index));
	const float s1 = sample_sdf_trilinear(global_voxels, to_voxel_sample_position(p1, lod_index));
	const float s2 = sample_sdf_trilinear(global_voxels, to_voxel_sample_position(p2, lod_index));
	return math::max(math::max(s0, s1), s2) >= -HIDDEN_SURFACE_SDF_THRESHOLD;
}

void cull_hidden_triangles(transvoxel::MeshArrays &mesh, const VoxelBuffer &global_voxels, const uint8_t lod_index) {
	if (mesh.indices.size() == 0) {
		return;
	}

	transvoxel::MeshArrays dst;

	for (unsigned int ii = 0; ii < mesh.indices.size(); ii += 3) {
		const int32_t i0 = mesh.indices[ii];
		const int32_t i1 = mesh.indices[ii + 1];
		const int32_t i2 = mesh.indices[ii + 2];

		if (!is_triangle_on_global_surface(
					global_voxels, mesh.vertices[i0], mesh.vertices[i1], mesh.vertices[i2], lod_index
			)) {
			continue;
		}

		const int32_t dst_i0 = dst.vertices.size();
		dst.vertices.push_back(mesh.vertices[i0]);
		dst.vertices.push_back(mesh.vertices[i1]);
		dst.vertices.push_back(mesh.vertices[i2]);
		dst.normals.push_back(mesh.normals[i0]);
		dst.normals.push_back(mesh.normals[i1]);
		dst.normals.push_back(mesh.normals[i2]);
		dst.lod_data.push_back(mesh.lod_data[i0]);
		dst.lod_data.push_back(mesh.lod_data[i1]);
		dst.lod_data.push_back(mesh.lod_data[i2]);
		dst.indices.push_back(dst_i0);
		dst.indices.push_back(dst_i0 + 1);
		dst.indices.push_back(dst_i0 + 2);
	}

	mesh = std::move(dst);
}

Vector2f get_planar_uv(const Vector3f position, const Vector3f normal, const float uv_scale) {
	const Vector3f an(Math::abs(normal.x), Math::abs(normal.y), Math::abs(normal.z));
	if (an.y >= an.x && an.y >= an.z) {
		return Vector2f(position.x, position.z) * uv_scale;
	}
	if (an.x >= an.y && an.x >= an.z) {
		return Vector2f(position.z, position.y) * uv_scale;
	}
	return Vector2f(position.x, position.y) * uv_scale;
}

void fill_surface_arrays(Array &arrays, const transvoxel::MeshArrays &src, const float uv_scale) {
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedFloat32Array lod_data;
	PackedInt32Array indices;

	copy_to(vertices, to_span_const(src.vertices));
	copy_to(normals, to_span_const(src.normals));
	copy_to(indices, to_span_const(src.indices));

	uvs.resize(src.vertices.size());
	Vector2 *uvs_w = uvs.ptrw();
	for (unsigned int i = 0; i < src.vertices.size(); ++i) {
		uvs_w[i] = to_vec2(get_planar_uv(src.vertices[i], src.normals[i], uv_scale));
	}

	lod_data.resize(src.lod_data.size() * 4);
	static_assert(sizeof(transvoxel::LodAttrib) == 16);
	memcpy(lod_data.ptrw(), src.lod_data.data(), lod_data.size() * sizeof(float));

	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = vertices;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_CUSTOM0] = lod_data;
	arrays[Mesh::ARRAY_INDEX] = indices;
}

void append_collision_surface(VoxelMesher::Output::CollisionSurface &collision, const transvoxel::MeshArrays &mesh) {
	const int32_t vertex_offset = collision.positions.size();

	append_array(collision.positions, mesh.vertices);
	for (const int32_t index : mesh.indices) {
		collision.indices.push_back(vertex_offset + index);
	}
}

void collect_visible_materials(
		MaterialSet &materials,
		const VoxelBuffer &voxels,
		const bool textures_ignore_air_voxels
) {
	const Vector3i size = voxels.get_size();
	for (int z = 0; z < size.z - 1; ++z) {
		for (int x = 0; x < size.x - 1; ++x) {
			for (int y = 0; y < size.y - 1; ++y) {
				uint8_t sign_mask = 0;
				uint8_t material_indices[8];
				float sdf_values[8];

				unsigned int ci = 0;
				for (int dz = 0; dz <= 1; ++dz) {
					for (int dx = 0; dx <= 1; ++dx) {
						for (int dy = 0; dy <= 1; ++dy) {
							const int vx = x + dx;
							const int vy = y + dy;
							const int vz = z + dz;
							const float sdf = voxels.get_voxel_f(vx, vy, vz, VoxelBuffer::CHANNEL_SDF);
							sdf_values[ci] = sdf;
							material_indices[ci] =
									static_cast<uint8_t>(voxels.get_voxel(vx, vy, vz, VoxelBuffer::CHANNEL_INDICES));
							if (sdf < 0.f) {
								sign_mask |= (1 << ci);
							}
							++ci;
						}
					}
				}

				if (sign_mask == 0 || sign_mask == 0xff) {
					continue;
				}

				for (unsigned int i = 0; i < 8; ++i) {
					if (textures_ignore_air_voxels && sdf_values[i] >= 0.f) {
						continue;
					}
					materials.used[material_indices[i]] = true;
				}
			}
		}
	}
}

void generate_filtered_sdf(
		VoxelBuffer &dst,
		const VoxelBuffer &src,
		const uint8_t material_id,
		const bool textures_ignore_air_voxels
) {
	dst.create(src.get_size());
	dst.set_channel_depth(VoxelBuffer::CHANNEL_SDF, src.get_channel_depth(VoxelBuffer::CHANNEL_SDF));

	const Vector3i size = src.get_size();
	for (int z = 0; z < size.z; ++z) {
		for (int x = 0; x < size.x; ++x) {
			for (int y = 0; y < size.y; ++y) {
				const float sdf = src.get_voxel_f(x, y, z, VoxelBuffer::CHANNEL_SDF);
				const uint8_t voxel_material =
						static_cast<uint8_t>(src.get_voxel(x, y, z, VoxelBuffer::CHANNEL_INDICES));

				float filtered_sdf;
				if (sdf < 0.f && voxel_material == material_id) {
					filtered_sdf = sdf;
				} else if (!textures_ignore_air_voxels && sdf >= 0.f && voxel_material == material_id) {
					filtered_sdf = sdf;
				} else {
					filtered_sdf = Math::abs(sdf);
					if (filtered_sdf < constants::QUANTIZED_SDF_16_BITS_SCALE) {
						filtered_sdf = constants::QUANTIZED_SDF_16_BITS_SCALE;
					}
				}

				dst.set_voxel_f(filtered_sdf, x, y, z, VoxelBuffer::CHANNEL_SDF);
			}
		}
	}
}

} // namespace

VoxelMesherTransvoxelMultiMaterial::VoxelMesherTransvoxelMultiMaterial() {
	set_padding(transvoxel::MIN_PADDING, transvoxel::MAX_PADDING);
}

VoxelMesherTransvoxelMultiMaterial::~VoxelMesherTransvoxelMultiMaterial() {}

void VoxelMesherTransvoxelMultiMaterial::build(VoxelMesher::Output &output, const VoxelMesher::Input &input) {
	ZN_PROFILE_SCOPE();

	Parameters params;
	{
		RWLockRead rlock(_parameters_lock);
		params = _parameters;
	}

	const VoxelBuffer &voxels = input.voxels;

	if (voxels.is_uniform(VoxelBuffer::CHANNEL_SDF)) {
		return;
	}

	if (voxels.get_channel_depth(VoxelBuffer::CHANNEL_INDICES) != VoxelBuffer::DEPTH_8_BIT) {
		ZN_PRINT_ERROR_ONCE("VoxelMesherTransvoxelMultiMaterial requires the Indices channel to use 8-bit depth.");
		return;
	}

	MaterialSet material_set;
	collect_visible_materials(material_set, voxels, params.textures_ignore_air_voxels);

	static thread_local transvoxel::Cache cache;
	static thread_local transvoxel::MeshArrays mesh_arrays;

	output.primitive_type = Mesh::PRIMITIVE_TRIANGLES;
	output.mesh_flags = (RenderingServerEnums::ARRAY_CUSTOM_RGBA_FLOAT << Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT);

	VoxelBuffer filtered_voxels(VoxelBuffer::ALLOCATOR_DEFAULT);

	for (unsigned int material_id = 0; material_id < material_set.used.size(); ++material_id) {
		if (!material_set.used[material_id]) {
			continue;
		}

		generate_filtered_sdf(filtered_voxels, voxels, material_id, params.textures_ignore_air_voxels);

		transvoxel::DefaultTextureIndicesData default_texture_indices_data = transvoxel::build_regular_mesh(
				filtered_voxels,
				VoxelBuffer::CHANNEL_SDF,
				input.lod_index,
				transvoxel::TEXTURES_NONE,
				cache,
				mesh_arrays,
				nullptr,
				params.edge_clamp_margin,
				params.textures_ignore_air_voxels
		);

		cull_hidden_triangles(mesh_arrays, voxels, input.lod_index);

		if (input.collision_hint && mesh_arrays.indices.size() > 0) {
			append_collision_surface(output.collision_surface, mesh_arrays);
		}

		if (params.transitions_enabled && input.lod_hint) {
			for (int dir = 0; dir < Cube::SIDE_COUNT; ++dir) {
				transvoxel::build_transition_mesh(
						filtered_voxels,
						VoxelBuffer::CHANNEL_SDF,
						dir,
						input.lod_index,
						transvoxel::TEXTURES_NONE,
						cache,
						mesh_arrays,
						default_texture_indices_data,
						params.edge_clamp_margin,
						params.textures_ignore_air_voxels
				);
			}
			cull_hidden_triangles(mesh_arrays, voxels, input.lod_index);
		}

		if (mesh_arrays.indices.size() == 0) {
			continue;
		}

		Array arrays;
		fill_surface_arrays(arrays, mesh_arrays, params.uv_scale);

		output.surfaces.push_back(Output::Surface());
		Output::Surface &surface = output.surfaces.back();
		surface.arrays = arrays;
		surface.material_index = material_id;
	}
}

int VoxelMesherTransvoxelMultiMaterial::get_used_channels_mask() const {
	return (1 << VoxelBuffer::CHANNEL_SDF) | (1 << VoxelBuffer::CHANNEL_INDICES);
}

bool VoxelMesherTransvoxelMultiMaterial::is_generating_collision_surface() const {
	return true;
}

void VoxelMesherTransvoxelMultiMaterial::set_library(Ref<VoxelSdfMaterialLibrary> library) {
	RWLockWrite wlock(_parameters_lock);
	if (_parameters.library == library) {
		return;
	}
	_parameters.library = library;
	emit_changed();
}

Ref<VoxelSdfMaterialLibrary> VoxelMesherTransvoxelMultiMaterial::get_library() const {
	RWLockRead rlock(_parameters_lock);
	return _parameters.library;
}

void VoxelMesherTransvoxelMultiMaterial::set_textures_ignore_air_voxels(bool enable) {
	RWLockWrite wlock(_parameters_lock);
	if (_parameters.textures_ignore_air_voxels == enable) {
		return;
	}
	_parameters.textures_ignore_air_voxels = enable;
	emit_changed();
}

bool VoxelMesherTransvoxelMultiMaterial::get_textures_ignore_air_voxels() const {
	RWLockRead rlock(_parameters_lock);
	return _parameters.textures_ignore_air_voxels;
}

void VoxelMesherTransvoxelMultiMaterial::set_transitions_enabled(bool enable) {
	RWLockWrite wlock(_parameters_lock);
	if (_parameters.transitions_enabled == enable) {
		return;
	}
	_parameters.transitions_enabled = enable;
	emit_changed();
}

bool VoxelMesherTransvoxelMultiMaterial::get_transitions_enabled() const {
	RWLockRead rlock(_parameters_lock);
	return _parameters.transitions_enabled;
}

void VoxelMesherTransvoxelMultiMaterial::set_edge_clamp_margin(float margin) {
	RWLockWrite wlock(_parameters_lock);
	_parameters.edge_clamp_margin = math::clamp(margin, 0.f, 0.5f);
	emit_changed();
}

float VoxelMesherTransvoxelMultiMaterial::get_edge_clamp_margin() const {
	RWLockRead rlock(_parameters_lock);
	return _parameters.edge_clamp_margin;
}

void VoxelMesherTransvoxelMultiMaterial::set_uv_scale(float scale) {
	RWLockWrite wlock(_parameters_lock);
	_parameters.uv_scale = math::max(scale, 0.0001f);
	emit_changed();
}

float VoxelMesherTransvoxelMultiMaterial::get_uv_scale() const {
	RWLockRead rlock(_parameters_lock);
	return _parameters.uv_scale;
}

Ref<Material> VoxelMesherTransvoxelMultiMaterial::get_material_by_index(unsigned int index) const {
	Ref<VoxelSdfMaterialLibrary> library = get_library();
	if (library.is_null()) {
		return Ref<Material>();
	}
	return library->get_material_by_index(index);
}

unsigned int VoxelMesherTransvoxelMultiMaterial::get_material_index_count() const {
	Ref<VoxelSdfMaterialLibrary> library = get_library();
	if (library.is_null()) {
		return 0;
	}
	return library->get_material_index_count();
}

#ifdef TOOLS_ENABLED
void VoxelMesherTransvoxelMultiMaterial::get_configuration_warnings(PackedStringArray &out_warnings) const {
	if (get_library().is_null()) {
		out_warnings.append(String(ZN_TTR("{0} has no {1} assigned."))
									.format(varray(
											VoxelMesherTransvoxelMultiMaterial::get_class_static(),
											VoxelSdfMaterialLibrary::get_class_static()
									)));
	}
}
#endif

void VoxelMesherTransvoxelMultiMaterial::_bind_methods() {
	using Self = VoxelMesherTransvoxelMultiMaterial;

	ClassDB::bind_method(D_METHOD("set_library", "library"), &Self::set_library);
	ClassDB::bind_method(D_METHOD("get_library"), &Self::get_library);

	ClassDB::bind_method(D_METHOD("set_textures_ignore_air_voxels", "enabled"), &Self::set_textures_ignore_air_voxels);
	ClassDB::bind_method(D_METHOD("get_textures_ignore_air_voxels"), &Self::get_textures_ignore_air_voxels);

	ClassDB::bind_method(D_METHOD("set_transitions_enabled", "enabled"), &Self::set_transitions_enabled);
	ClassDB::bind_method(D_METHOD("get_transitions_enabled"), &Self::get_transitions_enabled);

	ClassDB::bind_method(D_METHOD("set_edge_clamp_margin", "margin"), &Self::set_edge_clamp_margin);
	ClassDB::bind_method(D_METHOD("get_edge_clamp_margin"), &Self::get_edge_clamp_margin);

	ClassDB::bind_method(D_METHOD("set_uv_scale", "scale"), &Self::set_uv_scale);
	ClassDB::bind_method(D_METHOD("get_uv_scale"), &Self::get_uv_scale);

	ADD_GROUP("Materials", "");

	ADD_PROPERTY(
			PropertyInfo(
					Variant::OBJECT,
					"library",
					PROPERTY_HINT_RESOURCE_TYPE,
					VoxelSdfMaterialLibrary::get_class_static()
			),
			"set_library",
			"get_library"
	);

	ADD_PROPERTY(
			PropertyInfo(Variant::BOOL, "textures_ignore_air_voxels"),
			"set_textures_ignore_air_voxels",
			"get_textures_ignore_air_voxels"
	);

	ADD_GROUP("UVs", "uv_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "uv_scale", PROPERTY_HINT_RANGE, "0.0001,1000,0.001,or_greater"),
			"set_uv_scale", "get_uv_scale");

	ADD_GROUP("Advanced", "");
	ADD_PROPERTY(
			PropertyInfo(Variant::BOOL, "transitions_enabled"), "set_transitions_enabled", "get_transitions_enabled"
	);
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "edge_clamp_margin"), "set_edge_clamp_margin", "get_edge_clamp_margin");
}

} // namespace zylann::voxel
