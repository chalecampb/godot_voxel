#include "test_threaded_task_runner.h"
#include "../../util/containers/std_unordered_map.h"
#include "../../util/containers/std_vector.h"
#include "../../util/godot/classes/os.h"
#include "../../util/godot/classes/time.h"
#include "../../util/godot/core/random_pcg.h"
#include "../../util/io/log.h"
#include "../../util/io/std_string_text_writer.h"
#include "../../util/math/vector3i.h"
#include "../../util/memory/memory.h"
#include "../../util/profiling.h"
#include "../../util/string/format.h"
#include "../../util/tasks/threaded_task_runner.h"
#include "../../util/testing/test_macros.h"

// #define VOXEL_TEST_TASK_POSTPONING_DUMP_EVENTS
#ifdef VOXEL_TEST_TASK_POSTPONING_DUMP_EVENTS
#include <fstream>
#endif

namespace zylann::tests {

void test_threaded_task_runner_misc() {
	static const uint32_t task_duration_usec = 100'000;

	struct TaskCounter {
		std::atomic_uint32_t max_count = { 0 };
		std::atomic_uint32_t current_count = { 0 };
		std::atomic_uint32_t completed_count = { 0 };

		void reset() {
			max_count = 0;
			current_count = 0;
			completed_count = 0;
		}
	};

	class TestTask : public IThreadedTask {
	public:
		std::shared_ptr<TaskCounter> counter;
		bool completed = false;

		TestTask(std::shared_ptr<TaskCounter> p_counter) : counter(p_counter) {}

		void run(ThreadedTaskContext &ctx) override {
			ZN_PROFILE_SCOPE();
			ZN_ASSERT(counter != nullptr);

			++counter->current_count;

			// Update maximum count
			// https://stackoverflow.com/questions/16190078/how-to-atomically-update-a-maximum-value
			unsigned int current_count = counter->current_count;
			unsigned int prev_max = counter->max_count;
			while (prev_max < current_count && !counter->max_count.compare_exchange_weak(prev_max, current_count)) {
				current_count = counter->current_count;
			}

			Thread::sleep_usec(task_duration_usec);

			--counter->current_count;
			++counter->completed_count;
			completed = true;
		}

		void apply_result() override {
			ZN_TEST_ASSERT(completed);
		}
	};

	struct L {
		static void dequeue_tasks(ThreadedTaskRunner &runner) {
			runner.dequeue_completed_tasks([](IThreadedTask *task) {
				ZN_ASSERT(task != nullptr);
				task->apply_result();
				ZN_DELETE(task);
			});
		}
	};

	const unsigned int test_thread_count = 4;
	const unsigned int hw_concurrency = Thread::get_hardware_concurrency();
	if (hw_concurrency < test_thread_count) {
		ZN_PRINT_WARNING(format(
				"Hardware concurrency is {}, smaller than test requirement {}", test_thread_count, hw_concurrency
		));
	}

	std::shared_ptr<TaskCounter> parallel_counter = make_unique_instance<TaskCounter>();
	std::shared_ptr<TaskCounter> serial_counter = make_unique_instance<TaskCounter>();

	ThreadedTaskRunner runner;
	runner.set_thread_count(test_thread_count);
	runner.set_name("Test");

	// Parallel tasks only

	for (unsigned int i = 0; i < 16; ++i) {
		TestTask *task = ZN_NEW(TestTask(parallel_counter));
		runner.enqueue(task, false);
	}

	runner.wait_for_all_tasks();
	L::dequeue_tasks(runner);
	ZN_TEST_ASSERT(parallel_counter->completed_count == 16);
	ZN_TEST_ASSERT(parallel_counter->max_count <= test_thread_count);
	ZN_TEST_ASSERT(parallel_counter->current_count == 0);

	// Serial tasks only

	for (unsigned int i = 0; i < 16; ++i) {
		TestTask *task = ZN_NEW(TestTask(serial_counter));
		runner.enqueue(task, true);
	}

	runner.wait_for_all_tasks();
	L::dequeue_tasks(runner);
	ZN_TEST_ASSERT(serial_counter->completed_count == 16);
	ZN_TEST_ASSERT(serial_counter->max_count == 1);
	ZN_TEST_ASSERT(serial_counter->current_count == 0);

	// Interleaved

	parallel_counter->reset();
	serial_counter->reset();

	for (unsigned int i = 0; i < 32; ++i) {
		if ((i & 1) == 0) {
			TestTask *task = ZN_NEW(TestTask(parallel_counter));
			runner.enqueue(task, false);
		} else {
			TestTask *task = ZN_NEW(TestTask(serial_counter));
			runner.enqueue(task, true);
		}
	}

	runner.wait_for_all_tasks();
	L::dequeue_tasks(runner);
	ZN_TEST_ASSERT(parallel_counter->completed_count == 16);
	ZN_TEST_ASSERT(parallel_counter->max_count <= test_thread_count);
	ZN_TEST_ASSERT(parallel_counter->current_count == 0);
	ZN_TEST_ASSERT(serial_counter->completed_count == 16);
	ZN_TEST_ASSERT(serial_counter->max_count == 1);
	ZN_TEST_ASSERT(serial_counter->current_count == 0);
}

void test_threaded_task_runner_debug_names() {
	class NamedTestTask1 : public IThreadedTask {
	public:
		unsigned int sleep_amount_usec;

		NamedTestTask1(int p_sleep_amount_usec) : sleep_amount_usec(p_sleep_amount_usec) {}

		void run(ThreadedTaskContext &ctx) override {
			ZN_PROFILE_SCOPE();
			Thread::sleep_usec(sleep_amount_usec);
		}

		const char *get_debug_name() const override {
			return "NamedTestTask1";
		}
	};

	class NamedTestTask2 : public IThreadedTask {
	public:
		unsigned int sleep_amount_usec;

		NamedTestTask2(int p_sleep_amount_usec) : sleep_amount_usec(p_sleep_amount_usec) {}

		void run(ThreadedTaskContext &ctx) override {
			ZN_PROFILE_SCOPE();
			Thread::sleep_usec(sleep_amount_usec);
		}

		const char *get_debug_name() const override {
			return "NamedTestTask2";
		}
	};

	struct L {
		static void dequeue_tasks(ThreadedTaskRunner &runner, unsigned int &r_in_flight_count) {
			runner.dequeue_completed_tasks([&r_in_flight_count](IThreadedTask *task) {
				ZN_ASSERT(task != nullptr);
				task->apply_result();
				ZN_DELETE(task);
				ZN_ASSERT(r_in_flight_count > 0);
				--r_in_flight_count;
			});
		}
	};

	const unsigned int test_thread_count = 4;
	const unsigned int hw_concurrency = Thread::get_hardware_concurrency();
	if (hw_concurrency < test_thread_count) {
		ZN_PRINT_WARNING(format(
				"Hardware concurrency is {}, smaller than test requirement {}", test_thread_count, hw_concurrency
		));
	}

	ThreadedTaskRunner runner;
	runner.set_thread_count(test_thread_count);
	runner.set_name("Test");

	const uint64_t time_before = Time::get_singleton()->get_ticks_msec();

	unsigned int in_flight_count = 0;

	StdUnorderedMap<StdString, int> name_counts;

	while (Time::get_singleton()->get_ticks_msec() - time_before < 5000) {
		ZN_PROFILE_SCOPE();

		// Saturate task queue so a bunch should be running while we query their names
		while (in_flight_count < 5000) {
			for (unsigned int i = 0; i < 1000; ++i) {
				if ((i % 3) != 0) {
					NamedTestTask1 *task = ZN_NEW(NamedTestTask1(100 + i % 11));
					runner.enqueue(task, false);
				} else {
					NamedTestTask2 *task = ZN_NEW(NamedTestTask2(60 + i % 7));
					runner.enqueue(task, true);
				}
				++in_flight_count;
			}
		}

		// Get debug task names
		FixedArray<const char *, ThreadedTaskRunner::MAX_THREADS> active_task_names;
		fill(active_task_names, (const char *)nullptr);
		for (unsigned int i = 0; i < test_thread_count; ++i) {
			active_task_names[i] = runner.get_thread_debug_task_name(i);
		}

		// Put task names into an array
		StdVector<StdString> task_names;
		task_names.resize(test_thread_count);
		for (unsigned int i = 0; i < active_task_names.size(); ++i) {
			const char *name = active_task_names[i];
			if (name != nullptr) {
				task_names[i] = name;
			}
		}

		// Count names
		for (int i = 0; i < task_names.size(); ++i) {
			const StdString &name = task_names[i];
			auto it = name_counts.find(name);
			if (it == name_counts.end()) {
				name_counts.insert({ name, 0 });
			} else {
				it->second++;
			}
		}

		L::dequeue_tasks(runner, in_flight_count);

		OS::get_singleton()->delay_usec(10'000);
	}

	runner.wait_for_all_tasks();
	L::dequeue_tasks(runner, in_flight_count);

	// Print how many times each name came up.
	// Doing this to check if the test runs as expected and to prevent compiler optimization on getting the names
	StdStringTextWriter ss;
	for (auto it = name_counts.begin(); it != name_counts.end(); ++it) {
		ss << it->first << ": " << it->second << "; ";
	}
	print_line(ss.get_written());
}

void test_task_priority_values() {
	ZN_TEST_ASSERT(TaskPriority(0, 0, 0, 0) < TaskPriority(1, 0, 0, 0));
	ZN_TEST_ASSERT(TaskPriority(0, 0, 0, 0) < TaskPriority(0, 0, 0, 1));
	ZN_TEST_ASSERT(TaskPriority(10, 0, 0, 0) < TaskPriority(0, 10, 0, 0));
	ZN_TEST_ASSERT(TaskPriority(10, 10, 0, 0) < TaskPriority(10, 10, 10, 0));
}

// Simulates doing work in every chunk of a grid, where each task will want to access neighbors of each block. If any
// neighbor fails to get locked, the task is postponed.
void test_threaded_task_postponing() {
	// There isn't really a test check in this function, for now we run it to detect if it crashes and that all tasks
	// eventually run once.

	struct Block {
		std::atomic_bool is_locked;
	};

	struct Map {
		// Doesn't have to be a map but I chose it anyways since that's how the actual voxel map is stored
		StdUnorderedMap<Vector3i, Block> blocks;
	};

	struct Event {
		enum Type { BLOCK_BEGIN, BLOCK_END };
		Type type;
		Vector3i bpos;
		uint64_t time_us;
	};

	// To log what actually happened and visualize it
	struct EventList {
		StdVector<Event> events;
		Mutex mutex;

		inline void push(Event event) {
			event.time_us = Time::get_singleton()->get_ticks_usec();
			MutexLock lock(mutex);
			events.push_back(event);
		}
	};

	class Task1 : public IThreadedTask {
	public:
		unsigned int sleep_amount_usec;
		Vector3i bpos0;
		Map &map;
		EventList &events;

		Task1(int p_sleep_amount_usec, Map &p_map, Vector3i p_bpos, EventList &p_events) :
				sleep_amount_usec(p_sleep_amount_usec), bpos0(p_bpos), map(p_map), events(p_events) {}

		bool try_lock_area(StdVector<Block *> &locked_blocks) {
			Vector3i delta;
			for (delta.z = -1; delta.z < 2; ++delta.z) {
				for (delta.x = -1; delta.x < 2; ++delta.x) {
					for (delta.y = -1; delta.y < 2; ++delta.y) {
						const Vector3i bpos = bpos0 + delta;
						auto it = map.blocks.find(bpos);
						if (it == map.blocks.end()) {
							continue;
						}
						Block &block = it->second;
						bool expected = false;
						if (block.is_locked.compare_exchange_strong(expected, true) == false) {
							// Could not lock, will have to cancel
							for (Block *b : locked_blocks) {
								b->is_locked = false;
							}
							locked_blocks.clear();
							return false;
						} else {
							// Successful lock
							locked_blocks.push_back(&block);
						}
					}
				}
			}
			return true;
		}

		void run(ThreadedTaskContext &ctx) override {
			ZN_PROFILE_SCOPE();

			static thread_local StdVector<Block *> locked_blocks;

			if (!try_lock_area(locked_blocks)) {
				ctx.status = ThreadedTaskContext::STATUS_POSTPONED;
				return;
			}

#ifdef VOXEL_TEST_TASK_POSTPONING_DUMP_EVENTS
			events.push(Event{ Event::BLOCK_BEGIN, bpos0 });
#endif

			{
				ZN_PROFILE_SCOPE_NAMED("Work");
				Thread::sleep_usec(sleep_amount_usec);
			}

#ifdef VOXEL_TEST_TASK_POSTPONING_DUMP_EVENTS
			events.push(Event{ Event::BLOCK_END, bpos0 });
#endif

			for (Block *block : locked_blocks) {
				block->is_locked = false;
			}
			locked_blocks.clear();
		}

		const char *get_debug_name() const override {
			return "Task1";
		}
	};

	struct L {
		static void dequeue_tasks(ThreadedTaskRunner &runner, unsigned int &r_in_flight_count) {
			runner.dequeue_completed_tasks([&r_in_flight_count](IThreadedTask *task) {
				ZN_ASSERT(task != nullptr);
				task->apply_result();
				ZN_DELETE(task);
				ZN_ASSERT(r_in_flight_count > 0);
				--r_in_flight_count;
			});
		}
	};

	const unsigned int test_thread_count = 4;
	const unsigned int hw_concurrency = Thread::get_hardware_concurrency();
	if (hw_concurrency < test_thread_count) {
		ZN_PRINT_WARNING(format(
				"Hardware concurrency is {}, smaller than test requirement {}", test_thread_count, hw_concurrency
		));
	}

	ThreadedTaskRunner runner;
	runner.set_thread_count(test_thread_count);
	runner.set_name("Test");

	unsigned int in_flight_count = 0;

	// Generate map
	Map map;
	const int map_size = 16;
	Vector3i bpos;
	for (bpos.z = 0; bpos.z < map_size; ++bpos.z) {
		for (bpos.x = 0; bpos.x < map_size; ++bpos.x) {
			for (bpos.y = 0; bpos.y < map_size; ++bpos.y) {
				Block &block = map.blocks[bpos];
				block.is_locked = false;
			}
		}
	}

	EventList events;
	RandomPCG rng;

	// Schedule tasks that will want to access overlapping blocks
	for (bpos.z = 0; bpos.z < map_size; ++bpos.z) {
		for (bpos.x = 0; bpos.x < map_size; ++bpos.x) {
			for (bpos.y = 0; bpos.y < map_size; ++bpos.y) {
				Task1 *task = ZN_NEW(Task1(1000 + rng.rand(2000), map, bpos, events));
				runner.enqueue(task, false);
				++in_flight_count;
			}
		}
	}

	runner.wait_for_all_tasks();
	L::dequeue_tasks(runner, in_flight_count);

	ZN_TEST_ASSERT(in_flight_count == 0);

#ifdef VOXEL_TEST_TASK_POSTPONING_DUMP_EVENTS
	// Dump events
	std::ofstream ofs("ddd_block_tasks_test.json", std::ios::binary);
	if (ofs.good()) {
		ofs << "{\n";
		ofs << "\t\"events\": [\n";
		unsigned int i = 0;
		for (const Event &e : events.events) {
			if (i > 0) {
				ofs << ",\n";
			}
			ofs << "\t\t[" << e.type << ", " << e.time_us << ", " << e.bpos.x << ", " << e.bpos.y << ", " << e.bpos.z
				<< "]";
			++i;
		}
		ofs << "\n\t]\n}\n";
	}
#endif
}

namespace {

// A task that is postponed on every run (like a meshing task waiting for a shader to compile) until it is released.
class PostponedTestTask : public IThreadedTask {
public:
	struct Group {
		std::atomic_bool cancelled = { false };
		std::atomic_bool released = { false };
		std::atomic_uint32_t run_count = { 0 };
		std::atomic_uint32_t completed_runs = { 0 };
		// When true, cancellation is only discovered through `get_priority()`, like tasks which drop themselves when
		// no viewer needs them anymore.
		bool cancel_through_priority = false;
	};

	PostponedTestTask(std::shared_ptr<Group> p_group) : _group(p_group) {}

	void run(ThreadedTaskContext &ctx) override {
		++_group->run_count;
		if (!_group->released) {
			ctx.status = ThreadedTaskContext::STATUS_POSTPONED;
			Thread::sleep_usec(50);
			return;
		}
		++_group->completed_runs;
	}

	TaskPriority get_priority() override {
		if (_group->cancel_through_priority) {
			_cancelled_by_priority = _group->cancelled;
		}
		return TaskPriority::max();
	}

	bool is_cancelled() override {
		if (_group->cancel_through_priority) {
			return _cancelled_by_priority;
		}
		return _group->cancelled;
	}

private:
	std::shared_ptr<Group> _group;
	bool _cancelled_by_priority = false;
};

bool wait_until_no_remaining_tasks(const ThreadedTaskRunner &runner, uint32_t timeout_msec) {
	const uint32_t begin = Time::get_singleton()->get_ticks_msec();
	while (runner.get_debug_remaining_tasks() != 0) {
		if (Time::get_singleton()->get_ticks_msec() - begin > timeout_msec) {
			return false;
		}
		Thread::sleep_usec(1000);
	}
	return true;
}

unsigned int delete_completed_tasks(ThreadedTaskRunner &runner) {
	unsigned int count = 0;
	runner.dequeue_completed_tasks([&count](IThreadedTask *task) {
		ZN_DELETE(task);
		++count;
	});
	return count;
}

} // namespace

// Requirements: WRLD-08 (terrain free does not wait for queued work).
// Tasks which were postponed and then cancelled must leave the runner instead of being postponed forever. A group of
// unrelated tasks sharing the runner must be unaffected.
void test_threaded_task_runner_cancelled_postponed_tasks() {
	const unsigned int cancelled_count = 3000;
	const unsigned int kept_count = 8;

	std::shared_ptr<PostponedTestTask::Group> cancelled_group = make_shared_instance<PostponedTestTask::Group>();
	std::shared_ptr<PostponedTestTask::Group> kept_group = make_shared_instance<PostponedTestTask::Group>();

	ThreadedTaskRunner runner;
	runner.set_thread_count(4);
	runner.set_name("TestCancelPostponed");

	for (unsigned int i = 0; i < cancelled_count; ++i) {
		runner.enqueue(ZN_NEW(PostponedTestTask(cancelled_group)), false);
	}
	for (unsigned int i = 0; i < kept_count; ++i) {
		runner.enqueue(ZN_NEW(PostponedTestTask(kept_group)), false);
	}

	// Let every task be picked and postponed at least once
	const uint32_t begin = Time::get_singleton()->get_ticks_msec();
	while (cancelled_group->run_count < cancelled_count) {
		ZN_TEST_ASSERT_MSG(Time::get_singleton()->get_ticks_msec() - begin < 20000, "Tasks were not picked");
		Thread::sleep_usec(1000);
	}

	cancelled_group->cancelled = true;

	// Only the other group remains, which keeps being postponed
	const uint32_t begin2 = Time::get_singleton()->get_ticks_msec();
	while (runner.get_debug_remaining_tasks() != kept_count) {
		if (Time::get_singleton()->get_ticks_msec() - begin2 > 10000) {
			break;
		}
		Thread::sleep_usec(1000);
	}
	// Fails here, before waiting for tasks which would never complete
	ZN_TEST_ASSERT_MSG(runner.get_debug_remaining_tasks() == kept_count, "Cancelled postponed tasks must leave the runner");

	// Release the tasks of the group which was not cancelled so the runner can finish
	kept_group->released = true;
	const bool drained = wait_until_no_remaining_tasks(runner, 10000);
	ZN_TEST_ASSERT(drained);
	runner.wait_for_all_tasks();
	const unsigned int completed = delete_completed_tasks(runner);

	ZN_TEST_ASSERT(completed == cancelled_count + kept_count);
	ZN_TEST_ASSERT_MSG(kept_group->completed_runs == kept_count, "Tasks of another group must still complete");
	ZN_TEST_ASSERT_MSG(cancelled_group->completed_runs == 0, "Cancelled tasks must not run again");
}

// Requirements: WRLD-08 (blocks no viewer requests are dropped before they run).
// Postponed tasks must have their priority refreshed, so that tasks which only notice they are no longer needed when
// priority is evaluated are dropped too.
void test_threaded_task_runner_postponed_tasks_follow_priority_cancellation() {
	const unsigned int task_count = 500;

	std::shared_ptr<PostponedTestTask::Group> group = make_shared_instance<PostponedTestTask::Group>();
	group->cancel_through_priority = true;

	ThreadedTaskRunner runner;
	runner.set_thread_count(4);
	runner.set_name("TestPriorityPostponed");

	for (unsigned int i = 0; i < task_count; ++i) {
		runner.enqueue(ZN_NEW(PostponedTestTask(group)), false);
	}

	const uint32_t begin = Time::get_singleton()->get_ticks_msec();
	while (group->run_count < task_count) {
		ZN_TEST_ASSERT_MSG(Time::get_singleton()->get_ticks_msec() - begin < 20000, "Tasks were not picked");
		Thread::sleep_usec(1000);
	}

	group->cancelled = true;

	// Fails here, before waiting for tasks which would never complete
	const bool drained = wait_until_no_remaining_tasks(runner, 10000);
	ZN_TEST_ASSERT_MSG(drained, "Postponed tasks which are no longer needed must leave the runner");
	runner.wait_for_all_tasks();
	const unsigned int completed = delete_completed_tasks(runner);

	ZN_TEST_ASSERT(completed == task_count);
	ZN_TEST_ASSERT(group->completed_runs == 0);
}

} // namespace zylann::tests
