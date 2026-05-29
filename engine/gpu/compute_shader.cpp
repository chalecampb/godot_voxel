#include "compute_shader.h"
#include "../../util/godot/classes/directory.h"
#include "../../util/godot/classes/engine.h"
#include "../../util/godot/classes/file_access.h"
#include "../../util/godot/classes/rd_shader_source.h"
#include "../../util/godot/classes/rendering_server.h"
#include "../../util/godot/core/array.h" // for `varray` in GDExtension builds
#include "../../util/godot/core/packed_arrays.h"
#include "../../util/godot/core/print_string.h"
#include "../../util/godot/core/version.h"
#include "../../util/profiling.h"
#include "../../util/string/format.h"
#include "../voxel_engine.h"
#include <cstring>

namespace zylann::voxel {

String format_source_code_with_line_numbers(String src) {
	String dst;
	const PackedStringArray lines = src.split("\n", true);
	for (int i = 0; i < lines.size(); ++i) {
		const int line_number = i + 1;
		dst += String::num_int64(line_number);
		if (line_number < 10) {
			dst += "   ";
		} else if (line_number < 100) {
			dst += "  ";
		} else if (line_number < 1000) {
			dst += " ";
		}
		dst += "| ";
		dst += lines[i];
		dst += "\n";
	}
	return dst;
}

namespace {

constexpr const char *VOXEL_SHADER_CACHE_MAGIC = "VXCS";
constexpr uint32_t VOXEL_SHADER_CACHE_FORMAT_VERSION = 1;

String get_godot_build_identifier() {
#ifdef GODOT_VERSION_FULL_BUILD
	return GODOT_VERSION_FULL_BUILD;
#else
	return String::num_int64(GODOT_VERSION_MAJOR) + "." + String::num_int64(GODOT_VERSION_MINOR);
#endif
}

String get_voxel_shader_cache_directory() {
	String base_dir;
#if defined(ZN_GODOT)
	base_dir = Engine::get_singleton()->get_shader_cache_path();
#endif
	if (base_dir.is_empty()) {
		base_dir = "user://shader_cache";
	}
	return base_dir.path_join("voxel_compute");
}

struct VoxelShaderCacheKey {
	String source_hash;
	String shader_name;
	String shader_stage;
	String shader_language;
	String device_api_name;
	String device_pipeline_cache_uuid;
	String godot_build_identifier;

	String get_file_hash() const {
		const String text = source_hash + "|" + shader_name + "|" + shader_stage + "|" + shader_language + "|" +
				device_api_name + "|" + device_pipeline_cache_uuid + "|" + godot_build_identifier + "|" +
				String::num_int64(VOXEL_SHADER_CACHE_FORMAT_VERSION);
		return text.sha256_text();
	}
};

VoxelShaderCacheKey make_voxel_shader_cache_key(RenderingDevice &rd, const String &source_text, const String &name) {
	VoxelShaderCacheKey key;
	key.source_hash = source_text.sha256_text();
	key.shader_name = name;
	key.shader_stage = "compute";
	key.shader_language = "glsl";
	key.device_api_name = rd.get_device_api_name();
	key.device_pipeline_cache_uuid = rd.get_device_pipeline_cache_uuid();
	key.godot_build_identifier = get_godot_build_identifier();
	return key;
}

String get_voxel_shader_cache_file_path(const VoxelShaderCacheKey &key) {
	const String file_name = key.shader_name.validate_filename() + "." + key.get_file_hash() + ".cache";
	return get_voxel_shader_cache_directory().path_join(file_name);
}

bool validate_cache_string(FileAccess &f, const String &expected) {
	const String value = f.get_pascal_string();
	return value == expected;
}

bool load_voxel_shader_bytecode_from_cache(const String &path, const VoxelShaderCacheKey &key, PackedByteArray &out_bytecode) {
	Error open_error;
	Ref<FileAccess> f = zylann::godot::open_file(path, FileAccess::READ, open_error);
	if (f.is_null()) {
		return false;
	}

	uint8_t magic[4];
	if (zylann::godot::get_buffer(**f, Span<uint8_t>(magic, 4)) != 4) {
		return false;
	}
	if (memcmp(magic, VOXEL_SHADER_CACHE_MAGIC, 4) != 0) {
		return false;
	}

	if (f->get_32() != VOXEL_SHADER_CACHE_FORMAT_VERSION) {
		return false;
	}

	if (!validate_cache_string(**f, key.source_hash) || !validate_cache_string(**f, key.shader_name) ||
			!validate_cache_string(**f, key.shader_stage) || !validate_cache_string(**f, key.shader_language) ||
			!validate_cache_string(**f, key.device_api_name) ||
			!validate_cache_string(**f, key.device_pipeline_cache_uuid) ||
			!validate_cache_string(**f, key.godot_build_identifier)) {
		return false;
	}

	const uint32_t bytecode_size = f->get_32();
	const uint64_t remaining_bytes = f->get_length() - f->get_position();
	if (bytecode_size == 0 || bytecode_size > remaining_bytes) {
		return false;
	}

	out_bytecode.resize(bytecode_size);
	if (zylann::godot::get_buffer(**f, Span<uint8_t>(out_bytecode.ptrw(), bytecode_size)) != bytecode_size) {
		out_bytecode.resize(0);
		return false;
	}

	return true;
}

void save_voxel_shader_bytecode_to_cache(const String &path, const VoxelShaderCacheKey &key, const PackedByteArray &bytecode) {
	ERR_FAIL_COND(bytecode.is_empty());

	const String cache_dir = get_voxel_shader_cache_directory();
	const Error dir_error = DirAccess::make_dir_recursive_absolute(cache_dir);
	ERR_FAIL_COND_MSG(
			dir_error != OK,
			String("Could not create voxel shader cache directory '{0}'").format(varray(cache_dir))
	);

	Error open_error;
	Ref<FileAccess> f = zylann::godot::open_file(path, FileAccess::WRITE, open_error);
	ERR_FAIL_COND_MSG(f.is_null(), String("Could not open voxel shader cache file '{0}'").format(varray(path)));

	zylann::godot::store_buffer(**f, Span<const uint8_t>(reinterpret_cast<const uint8_t *>(VOXEL_SHADER_CACHE_MAGIC), 4));
	f->store_32(VOXEL_SHADER_CACHE_FORMAT_VERSION);
	f->store_pascal_string(key.source_hash);
	f->store_pascal_string(key.shader_name);
	f->store_pascal_string(key.shader_stage);
	f->store_pascal_string(key.shader_language);
	f->store_pascal_string(key.device_api_name);
	f->store_pascal_string(key.device_pipeline_cache_uuid);
	f->store_pascal_string(key.godot_build_identifier);
	ERR_FAIL_COND(bytecode.size() > UINT32_MAX);
	f->store_32(static_cast<uint32_t>(bytecode.size()));
	zylann::godot::store_buffer(**f, to_span(bytecode));
}

} // namespace

RID load_compute_shader_from_glsl(RenderingDevice &rd, String source_text, String name) {
	ZN_PRINT_VERBOSE(format("Creating VoxelRD compute shader {}", name));
	// For debugging
	// {
	// 	Ref<FileAccess> f = FileAccess::open("debug_" + name + ".txt", FileAccess::WRITE);
	// 	ZN_ASSERT(f.is_valid());
	// 	f->store_string(source_text);
	// }

	Ref<RDShaderSource> shader_source;
	shader_source.instantiate();
	shader_source->set_language(RenderingDevice::SHADER_LANGUAGE_GLSL);
	shader_source->set_stage_source(RenderingDevice::SHADER_STAGE_COMPUTE, source_text);

	// ZN_ASSERT_RETURN_MSG(
	// 		VoxelEngine::get_singleton().has_rendering_device(),
	// 		format("Can't create compute shader \"{}\". Maybe the selected renderer doesn't support it? ({})",
	// 			   name,
	// 			   zylann::godot::get_current_rendering_method())
	// );
	// MutexLock mlock(VoxelEngine::get_singleton().get_rendering_device_mutex());

	const VoxelShaderCacheKey cache_key = make_voxel_shader_cache_key(rd, source_text, name);
	const String cache_file_path = get_voxel_shader_cache_file_path(cache_key);

	PackedByteArray shader_bytecode;
	if (load_voxel_shader_bytecode_from_cache(cache_file_path, cache_key, shader_bytecode)) {
		RID shader_rid = zylann::godot::shader_create_from_bytecode(rd, shader_bytecode);
		if (shader_rid.is_valid()) {
			ZN_PRINT_VERBOSE(format("Loaded VoxelRD compute shader {} from cache", name));
			return shader_rid;
		}
		ZN_PRINT_VERBOSE(format("VoxelRD compute shader cache rejected by RenderingDevice for {}", name));
	}

	Ref<RDShaderSPIRV> shader_spirv = zylann::godot::shader_compile_spirv_from_source(rd, **shader_source, true);
	ERR_FAIL_COND_V(shader_spirv.is_null(), RID());

	const String error_message = shader_spirv->get_stage_compile_error(RenderingDevice::SHADER_STAGE_COMPUTE);
	if (error_message != "") {
		ERR_PRINT(String("Failed to compile compute shader '{0}'").format(varray(name)));
		::print_line(error_message);

		if (is_verbose_output_enabled()) {
			const String formatted_source_text = format_source_code_with_line_numbers(source_text);
			::print_line(formatted_source_text);
		}

		return RID();
	}

	shader_bytecode = zylann::godot::shader_compile_binary_from_spirv(rd, **shader_spirv, name);
	ERR_FAIL_COND_V(shader_bytecode.is_empty(), RID());

	save_voxel_shader_bytecode_to_cache(cache_file_path, cache_key, shader_bytecode);

	const RID shader_rid = zylann::godot::shader_create_from_bytecode(rd, shader_bytecode);
	ERR_FAIL_COND_V(!shader_rid.is_valid(), RID());

	return shader_rid;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ComputeShaderInternal::clear(RenderingDevice &rd) {
	if (rid.is_valid()) {
		ZN_PROFILE_SCOPE();
#if DEBUG_ENABLED
		ZN_PRINT_VERBOSE(format("Freeing VoxelRD compute shader {}", debug_name));
#else
		ZN_PRINT_VERBOSE("Freeing VoxelRD compute shader");
#endif
		zylann::godot::free_rendering_device_rid(rd, rid);
		rid = RID();
	}
}

void ComputeShaderInternal::load_from_glsl(RenderingDevice &rd, String source_text, String name) {
	ZN_PROFILE_SCOPE();
	clear(rd);
	rid = load_compute_shader_from_glsl(rd, source_text, name);
#if DEBUG_ENABLED
	debug_name = name;
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ComputeShader::~ComputeShader() {
	ComputeShaderInternal internal = _internal;
	VoxelEngine::get_singleton().push_gpu_task_f([internal](GPUTaskContext &ctx) { //
		// *sigh*
		ComputeShaderInternal internal2 = internal;
		internal2.clear(ctx.rendering_device);
	});
}

std::shared_ptr<ComputeShader> ComputeShaderFactory::create_from_glsl(String source_text, String name) {
	std::shared_ptr<ComputeShader> shader = make_shared_instance<ComputeShader>();
	VoxelEngine::get_singleton().push_gpu_task_f([shader, source_text, name](GPUTaskContext &ctx) {
		shader->_internal.load_from_glsl(ctx.rendering_device, source_text, name);
		shader->_compilation_successful.store(shader->_internal.is_valid());
		shader->_compilation_complete.store(true);
	});
	return shader;
}

std::shared_ptr<ComputeShader> ComputeShaderFactory::create_invalid() {
	std::shared_ptr<ComputeShader> shader = make_shared_instance<ComputeShader>();
	shader->_compilation_complete.store(true);
	return shader;
}

RID ComputeShader::get_rid() const {
	// TODO Assert that we are on the GPU tasks thread
	return _internal.rid;
}

bool ComputeShader::is_compilation_complete() const {
	return _compilation_complete.load();
}

bool ComputeShader::is_compilation_successful() const {
	return _compilation_successful.load();
}

// std::shared_ptr<ComputeShader> ComputeShader::create_invalid() {
// 	return make_shared_instance<ComputeShader>(RID());
// }

} // namespace zylann::voxel
