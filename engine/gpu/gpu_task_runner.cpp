#include "gpu_task_runner.h"
#include "../../util/dstack.h"
#include "../../util/errors.h"
#include "../../util/godot/classes/rd_sampler_state.h"
#include "../../util/godot/classes/rendering_device.h"
#include "../../util/godot/classes/rendering_server.h"
#include "../../util/math/funcs.h"
#include "../../util/memory/memory.h"
#include "../../util/profiling.h"

#include "../../shaders/shaders.h"

namespace zylann::voxel {

GPUTaskRunner::GPUTaskRunner() {}

GPUTaskRunner::~GPUTaskRunner() {
	stop();

	// There shouldn't be any tasks at this point, we delete them in the thread before destroying the RenderingDevice.
	// But in theory nothing prevents tasks from being added yet after that...
	for (IGPUTask *task : _shared_tasks) {
		ZN_DELETE(task);
	}
}

void GPUTaskRunner::set_config(Config config) {
	_config = config;
	_config.max_in_flight_batches = math::clamp(_config.max_in_flight_batches, 1u, 4u);
}

void GPUTaskRunner::start() {
	ZN_ASSERT(!_running);
	ZN_PRINT_VERBOSE("Starting GPUTaskRunner");
	_running = true;
	_thread.start(
			[](void *p_userdata) {
				GPUTaskRunner *runner = static_cast<GPUTaskRunner *>(p_userdata);
				runner->thread_func();
			},
			this
	);
}

void GPUTaskRunner::stop() {
	if (!_running) {
		ZN_PRINT_VERBOSE("GPUTaskRunner::stop() was called but it wasn't running.");
		return;
	}
	_running = false;
	_semaphore.post();
	_thread.wait_to_finish();
}

bool GPUTaskRunner::is_running() const {
	return _running;
}

void GPUTaskRunner::push(IGPUTask *task) {
	ZN_ASSERT_RETURN(task != nullptr);
	MutexLock mlock(_mutex);
	_shared_tasks.push_back(task);
	_semaphore.post();
	++_pending_count;
}

unsigned int GPUTaskRunner::get_pending_task_count() const {
	return _pending_count;
}

// RenderingDevice *GPUTaskRunner::get_rendering_device() const {
// 	MutexLock mlock(_rendering_device_ptr_mutex);
// 	return _rendering_device;
// }

RID GPUComputePipelineCache::get_or_create(RenderingDevice &rd, RID shader_rid) {
	for (const Entry &entry : _entries) {
		if (entry.shader_rid == shader_rid) {
			return entry.pipeline_rid;
		}
	}

	const RID pipeline_rid = rd.compute_pipeline_create(shader_rid);
	ERR_FAIL_COND_V(!pipeline_rid.is_valid(), RID());

	_entries.push_back(Entry{ shader_rid, pipeline_rid });
	return pipeline_rid;
}

void GPUComputePipelineCache::clear(RenderingDevice &rd) {
	for (const Entry &entry : _entries) {
		godot::free_rendering_device_rid(rd, entry.pipeline_rid);
	}
	_entries.clear();
}

namespace {

size_t find_gpu_batch_end(
		Span<IGPUTask *> tasks,
		size_t begin_index,
		uint64_t shared_memory_limit,
		unsigned int fallback_task_limit,
		unsigned int batch_budget_multiplier
) {
	const uint64_t output_budget_bytes =
			math::max<uint64_t>(1, shared_memory_limit) * 1024 * math::max(batch_budget_multiplier, 1u);
	const unsigned int task_limit = fallback_task_limit * math::max(batch_budget_multiplier, 1u);

	uint64_t output_bytes = 0;
	size_t end_index = begin_index;

	for (; end_index < tasks.size(); ++end_index) {
		IGPUTask *task = tasks[end_index];
		const uint64_t task_output_bytes = task->get_required_shared_output_buffer_size();

		if (
				end_index > begin_index &&
				(end_index - begin_index >= task_limit || output_bytes + task_output_bytes > output_budget_bytes)
		) {
			break;
		}

		output_bytes += task_output_bytes;
	}

	return math::max(begin_index + 1, end_index);
}

} // namespace

void GPUTaskRunner::thread_func() {
	ZN_PROFILE_SET_THREAD_NAME("Voxel GPU tasks");
	ZN_DSTACK();

	{
		ZN_PRINT_VERBOSE("Creating Voxel RenderingDevice");
		// MutexLock mlock(_rendering_device_ptr_mutex);
		// We have to create this RenderingDevice in the same thread where we'll use it in, because otherwise it
		// triggers errors from threading guards in some of its methods.
		// This in turn affects a lot of other design decisions regarding how we manage resources with it...
		_rendering_device = RenderingServer::get_singleton()->create_local_rendering_device();
	}

	if (_rendering_device == nullptr) {
		ZN_PRINT_VERBOSE("Could not create local RenderingDevice, GPU functionality won't be supported.");
		return;
	}

	_storage_buffer_pool.set_rendering_device(_rendering_device);
	_base_resources.load(*_rendering_device);
	const uint64_t shared_memory_limit =
			_rendering_device->limit_get(RenderingDevice::LIMIT_MAX_COMPUTE_SHARED_MEMORY_SIZE);

	StdVector<IGPUTask *> tasks;

	// We use a common output buffer for tasks that need to download results back to the CPU,
	// because a single call to `buffer_get_data` is cheaper than multiple ones, due to Godot's API being synchronous.
	RID shared_output_storage_buffer_rid;
	unsigned int shared_output_storage_buffer_capacity = 0;
	struct SBRange {
		unsigned int position;
		unsigned int size;
	};
	StdVector<SBRange> shared_output_storage_buffer_segments;

	// Godot does not support async compute, so in order to get results from a compute shader, the only way is to sync
	// with the device, waiting for everything to complete. So instead of running one shader at a time, we run a few of
	// them.
	// It's also unclear how much to execute per frame.
	// 4 tasks was good enough on an nVidia 1060 for detail rendering, but for tasks with different costs it might need
	// different quota to prevent rendering slowdowns...
	const unsigned int fallback_batch_count = 16;

	while (_running) {
		{
			MutexLock mlock(_mutex);
			tasks = std::move(_shared_tasks);
		}
		if (tasks.size() == 0) {
			_semaphore.wait();
			continue;
		}

		ZN_ASSERT(_rendering_device != nullptr);
		GPUTaskContext ctx(*_rendering_device, _storage_buffer_pool, _base_resources, _pipeline_cache);

		for (size_t begin_index = 0; begin_index < tasks.size();) {
			ZN_PROFILE_SCOPE_NAMED("Batch");

			const size_t end_index = find_gpu_batch_end(
					to_span(tasks),
					begin_index,
					shared_memory_limit,
					fallback_batch_count,
					_config.max_in_flight_batches
			);

			unsigned int required_shared_output_buffer_size = 0;
			shared_output_storage_buffer_segments.clear();

			// Get how much data we'll want to download from the GPU for this batch
			for (size_t i = begin_index; i < end_index; ++i) {
				IGPUTask *task = tasks[i];
				const unsigned size = task->get_required_shared_output_buffer_size();
				// TODO Should we pad sections with some kind of alignment?
				shared_output_storage_buffer_segments.push_back(SBRange{ required_shared_output_buffer_size, size });
				required_shared_output_buffer_size += size;
			}

			// Make sure we allocate a storage buffer that can contain all output data in this batch
			if (required_shared_output_buffer_size > shared_output_storage_buffer_capacity) {
				ZN_PROFILE_SCOPE_NAMED("Resize shared output buffer");
				if (shared_output_storage_buffer_rid.is_valid()) {
					godot::free_rendering_device_rid(ctx.rendering_device, shared_output_storage_buffer_rid);
				}
				// TODO Resize to some multiplier above?
				shared_output_storage_buffer_rid =
						ctx.rendering_device.storage_buffer_create(required_shared_output_buffer_size);
				shared_output_storage_buffer_capacity = required_shared_output_buffer_size;
				ZN_ASSERT_CONTINUE(shared_output_storage_buffer_rid.is_valid());
			}

			ctx.shared_output_buffer_rid = shared_output_storage_buffer_rid;

			// Prepare tasks
			for (size_t i = begin_index; i < end_index; ++i) {
				ZN_PROFILE_SCOPE_NAMED("GPU Task Prepare");

				const SBRange range = shared_output_storage_buffer_segments[i - begin_index];
				ctx.shared_output_buffer_begin = range.position;
				ctx.shared_output_buffer_size = range.size;

				IGPUTask *task = tasks[i];
				task->prepare(ctx);
			}

			// Submit work and wait for completion
			{
				ZN_PROFILE_SCOPE_NAMED("RD Submit");
				ctx.rendering_device.submit();
			}
			{
				ZN_PROFILE_SCOPE_NAMED("RD Sync");
				ctx.rendering_device.sync();
			}

			// Download data from shared buffer
			if (required_shared_output_buffer_size > 0 && shared_output_storage_buffer_rid.is_valid()) {
				ZN_PROFILE_SCOPE_NAMED("Download shared output buffer");
				// Unfortunately we can't re-use memory for that buffer, Godot will always want to allocate it using
				// malloc. That buffer can be a few megabytes long...
				ctx.downloaded_shared_output_data = ctx.rendering_device.buffer_get_data(
						shared_output_storage_buffer_rid, 0, required_shared_output_buffer_size
				);
			}

			// Collect results and complete tasks
			for (size_t i = begin_index; i < end_index; ++i) {
				ZN_PROFILE_SCOPE_NAMED("GPU Task Collect");

				const SBRange range = shared_output_storage_buffer_segments[i - begin_index];
				ctx.shared_output_buffer_begin = range.position;
				ctx.shared_output_buffer_size = range.size;

				IGPUTask *task = tasks[i];
				task->collect(ctx);
				ZN_DELETE(task);
				--_pending_count;
			}

			ctx.downloaded_shared_output_data = PackedByteArray();

			begin_index = end_index;
		}

		tasks.clear();
	}

	ZN_ASSERT(tasks.size() == 0);

	// Cleanup

	if (shared_output_storage_buffer_rid.is_valid()) {
		godot::free_rendering_device_rid(*_rendering_device, shared_output_storage_buffer_rid);
	}

	{
		MutexLock mlock(_mutex);
		for (IGPUTask *task : _shared_tasks) {
			ZN_DELETE(task);
		}
	}

	_pipeline_cache.clear(*_rendering_device);
	_base_resources.clear(*_rendering_device);

	if (is_verbose_output_enabled()) {
		_storage_buffer_pool.debug_print();
	}

	_storage_buffer_pool.clear();
	_storage_buffer_pool.set_rendering_device(nullptr);

	{
		ZN_PRINT_VERBOSE("Freeing Voxel RenderingDevice");
		// MutexLock mlock(_rendering_device_ptr_mutex);
		memdelete(_rendering_device);
		_rendering_device = nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void BaseGPUResources::load(RenderingDevice &rd) {
	ZN_PROFILE_SCOPE();
	{
		ZN_PROFILE_SCOPE_NAMED("Base Compute Shaders");

		ZN_PRINT_VERBOSE("Loading VoxelEngine shaders");

		dilate_normalmap_shader.load_from_glsl(rd, g_dilate_normalmap_shader, "zylann.voxel.dilate_normalmap");
		detail_gather_hits_shader.load_from_glsl(rd, g_detail_gather_hits_shader, "zylann.voxel.detail_gather_hits");
		detail_normalmap_shader.load_from_glsl(rd, g_detail_normalmap_shader, "zylann.voxel.detail_normalmap_shader");

		detail_modifier_sphere_shader.load_from_glsl(
				rd,
				String(g_detail_modifier_shader_template_0) + String(g_modifier_sphere_shader_snippet) +
						String(g_detail_modifier_shader_template_1),
				"zylann.voxel.detail_modifier_sphere_shader"
		);

		detail_modifier_mesh_shader.load_from_glsl(
				rd,
				String(g_detail_modifier_shader_template_0) + String(g_modifier_mesh_shader_snippet) +
						String(g_detail_modifier_shader_template_1),
				"zylann.voxel.detail_modifier_mesh_shader"
		);

		block_modifier_sphere_shader.load_from_glsl(
				rd,
				String(g_block_modifier_shader_template_0) + String(g_modifier_sphere_shader_snippet) +
						String(g_block_modifier_shader_template_1),
				"zylann.voxel.block_modifier_sphere_shader"
		);

		block_modifier_mesh_shader.load_from_glsl(
				rd,
				String(g_block_modifier_shader_template_0) + String(g_modifier_mesh_shader_snippet) +
						String(g_block_modifier_shader_template_1),
				"zylann.voxel.block_modifier_mesh_shader"
		);
	}

	{
		Ref<RDSamplerState> sampler_state;
		sampler_state.instantiate();
		// Using samplers for their interpolation features.
		// Otherwise I don't feel like there is a point in using one IMO.
		sampler_state->set_mag_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
		sampler_state->set_min_filter(RenderingDevice::SAMPLER_FILTER_LINEAR);
		filtering_sampler_rid = zylann::godot::sampler_create(rd, **sampler_state);
	}
}

void BaseGPUResources::clear(RenderingDevice &rd) {
	dilate_normalmap_shader.clear(rd);
	detail_gather_hits_shader.clear(rd);
	detail_normalmap_shader.clear(rd);
	detail_modifier_sphere_shader.clear(rd);
	detail_modifier_mesh_shader.clear(rd);
	block_modifier_sphere_shader.clear(rd);
	block_modifier_mesh_shader.clear(rd);

	zylann::godot::free_rendering_device_rid(rd, filtering_sampler_rid);
	filtering_sampler_rid = RID();
}

} // namespace zylann::voxel
