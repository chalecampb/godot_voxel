#ifndef ZN_TEST_THREADED_TASK_RUNNER_H
#define ZN_TEST_THREADED_TASK_RUNNER_H

namespace zylann::tests {

void test_threaded_task_runner_misc();
void test_threaded_task_runner_debug_names();
void test_task_priority_values();
void test_threaded_task_postponing();
void test_threaded_task_runner_cancelled_postponed_tasks();
void test_threaded_task_runner_postponed_tasks_follow_priority_cancellation();

} // namespace zylann::tests

#endif // ZN_TEST_THREADED_TASK_RUNNER_H
