#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#define __ref
#define CONFIG_CGROUP_PERF 1
#define SINGLE_DEPTH_NESTING 1
#define EVENT_ALL 3
#define PERF_EVENT_STATE_ACTIVE 1
#define PERF_EVENT_STATE_OFF (-1)
#define PERF_EVENT_STATE_ERROR (-2)
#define PERF_EVENT_STATE_INACTIVE 0
#define PERF_ATTACH_CONTEXT 1
#define WARN_ON_ONCE(value) assert(!(value))
#define EXPORT_SYMBOL_GPL(symbol)
struct mock_lock { bool held; };
struct perf_event;
struct pmu;
struct perf_cgroup { unsigned identity; };
struct task_struct { unsigned identity; };
struct perf_event_context {
    struct task_struct *task;
    struct perf_event_context *parent_ctx;
    struct mock_lock mutex, lock;
    struct pmu *pmu;
    unsigned nr_events, nr_cgroups, nr_branch_stack, nr_stat;
    bool is_active;
};
struct perf_cpu_context {
    struct perf_event_context ctx;
    struct perf_event_context *task_ctx;
    struct pmu *unique_pmu;
    struct perf_cgroup *cgrp;
    bool quiesced;
};
struct pmu { struct perf_cpu_context *pmu_cpu_context; unsigned index; };
struct mock_node { unsigned unused; };
struct perf_event {
    struct perf_event_context *ctx;
    int cpu, state;
    unsigned attach_state;
    struct perf_cgroup *cgrp;
    struct perf_event *group_leader;
    struct mock_node event_entry, group_entry;
    struct { bool inherit_stat; } attr;
    bool context_attached, group_attached, pmu_owned, freed;
};
struct remove_event { struct perf_event *event; bool detach_group; };
static struct pmu *pmus[3];
static struct perf_cpu_context contexts[4];
static struct perf_event retained;
static struct task_struct boot_task;
static struct task_struct *current = &boot_task;
static struct perf_cgroup boot_cgroup = {1};
static unsigned executing_cpu, hotplug_readers, frees, callbacks, quiesces, resumes;
static unsigned rotations_started, rotations_stopped;
static bool irqs_off, target_online;
#define list_for_each_entry_rcu(iterator, head, member) \
    for (unsigned item_index = 0; item_index < 2 && ((iterator) = pmus[item_index]); item_index++)
#define per_cpu_ptr(pointer, cpu) (&(pointer)[cpu])
#define this_cpu_ptr(pointer) (&(pointer)[executing_cpu])
static void rcu_read_lock(void) {}
static void rcu_read_unlock(void) {}
static bool irqs_disabled(void) { return irqs_off; }
static void get_online_cpus(void) { hotplug_readers++; }
static void put_online_cpus(void) { assert(hotplug_readers); hotplug_readers--; }
static void mutex_lock_nested(struct mock_lock *lock, int nesting)
{ assert(nesting == SINGLE_DEPTH_NESTING); assert(!lock->held); lock->held = true; }
static void mutex_unlock(struct mock_lock *lock)
{ assert(lock->held); lock->held = false; }
static void lockdep_assert_held(struct mock_lock *lock) { assert(lock->held); }
static void raw_spin_lock_irq(struct mock_lock *lock)
{ assert(!lock->held); lock->held = true; irqs_off = true; }
static void raw_spin_unlock_irq(struct mock_lock *lock)
{ assert(lock->held); lock->held = false; irqs_off = false; }
static void perf_ctx_lock(struct perf_cpu_context *context, struct perf_event_context *task)
{ assert(!context->ctx.lock.held); context->ctx.lock.held = true; if (task) { assert(!task->lock.held); task->lock.held = true; } }
static void perf_ctx_unlock(struct perf_cpu_context *context, struct perf_event_context *task)
{ assert(context->ctx.lock.held); context->ctx.lock.held = false; if (task) { assert(task->lock.held); task->lock.held = false; } }
static void perf_pmu_disable(struct pmu *pmu) { assert(pmu); }
static void perf_pmu_enable(struct pmu *pmu) { assert(pmu); }
static void perf_pmu_rotate_stop(struct pmu *pmu)
{ assert(pmu && irqs_off); rotations_stopped++; }
static void perf_pmu_rotate_start(struct pmu *pmu)
{ assert(pmu && irqs_off); rotations_started++; }
static void cpu_ctx_sched_out(struct perf_cpu_context *context, int mode)
{
    assert(context == &contexts[executing_cpu] && mode == EVENT_ALL);
    assert(context->ctx.lock.held);
    context->ctx.is_active = false;
    if (retained.context_attached && retained.cpu == (int)executing_cpu) {
        retained.pmu_owned = false;
        retained.state = 0;
    }
    quiesces++;
}
static void task_ctx_sched_out(struct perf_event_context *context)
{ assert(context->lock.held); context->is_active = false; contexts[executing_cpu].task_ctx = NULL; }
static struct perf_cgroup *perf_cgroup_from_task(struct task_struct *task)
{ assert(task == current); return &boot_cgroup; }
static void perf_event_sched_in(struct perf_cpu_context *context,
                               struct perf_event_context *task, struct task_struct *running)
{
    assert(context->ctx.lock.held && running == current);
    assert(!context->quiesced);
    if (task) assert(task->lock.held);
    context->ctx.is_active = true;
    if (retained.context_attached) {
        retained.pmu_owned = true;
        retained.state = PERF_EVENT_STATE_ACTIVE;
    }
    resumes++;
}
static void perf_group_detach(struct perf_event *event)
{ assert(event->ctx->mutex.held && event->ctx->lock.held); event->group_attached = false; }
static bool is_cgroup_event(struct perf_event *event) { return event->cgrp != NULL; }
static bool has_branch_stack(struct perf_event *event) { (void)event; return false; }
static void update_group_times(struct perf_event *event) { assert(event->ctx->lock.held); }
static void list_del_init(struct mock_node *node) { (void)node; }
static void list_del_rcu(struct mock_node *node)
{
    struct perf_event *event = (struct perf_event *)((char *)node - offsetof(struct perf_event, event_entry));
    assert(event->ctx->mutex.held && event->ctx->lock.held && !event->pmu_owned);
    event->context_attached = false;
}
static void list_del_event(struct perf_event *event, struct perf_event_context *context);
static int __perf_remove_from_context(void *information)
{
    struct remove_event *removal = information;
    struct perf_event *event = removal->event;
    assert(target_online && executing_cpu == (unsigned)event->cpu);
    callbacks++;
    raw_spin_lock_irq(&event->ctx->lock);
    event->pmu_owned = false;
    event->state = 0;
    if (removal->detach_group) perf_group_detach(event);
    list_del_event(event, event->ctx);
    raw_spin_unlock_irq(&event->ctx->lock);
    return 0;
}
static int cpu_function_call(int cpu, int (*callback)(void *), void *info)
{
    unsigned previous_cpu = executing_cpu;
    int result;
    assert(cpu == retained.cpu);
    if (!target_online) return -ENXIO;
    executing_cpu = (unsigned)cpu;
    result = callback(info);
    executing_cpu = previous_cpu;
    return result;
}
static int task_function_call(struct task_struct *task, int (*callback)(void *), void *info)
{ (void)task; (void)callback; (void)info; return -ESRCH; }
static void free_event(struct perf_event *event)
{
    assert(!event->ctx->mutex.held && !event->ctx->lock.held);
    assert(!event->context_attached && !event->group_attached && !event->pmu_owned);
    assert(!event->freed && hotplug_readers);
    event->freed = true;
    frees++;
}

static int __perf_event_disable(void *information)
{ struct perf_event *event=information; event->state=PERF_EVENT_STATE_OFF; return 0; }
static int __perf_event_enable(void *information)
{ struct perf_event *event=information; event->state=PERF_EVENT_STATE_ACTIVE; return 0; }
static void __perf_event_mark_enabled(struct perf_event *event)
{ assert(event->ctx->lock.held); event->state=PERF_EVENT_STATE_INACTIVE; }
