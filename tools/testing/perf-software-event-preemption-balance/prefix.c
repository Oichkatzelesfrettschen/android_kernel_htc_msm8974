#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t u32;
typedef uint64_t u64;
struct pt_regs { int unused; };
struct perf_sample_data { u64 address; };
struct swevent_htable { int recursion[4]; };
static struct swevent_htable swevent_htable;
static int context_kind, preemption_depth;
static unsigned disable_calls, enable_calls, sample_calls, callback_calls;
static unsigned acquired_slots, released_slots;
static unsigned nested_mode;
#define PERF_TYPE_SOFTWARE 1
#define __get_cpu_var(variable) (variable)
static void audit_recursion_slot(int slot, const char *function)
{
    assert(slot >= 0 && slot < 4);
    assert(swevent_htable.recursion[slot] == 1);
    if (strcmp(function, "put_recursion_context") == 0)
        released_slots++;
    else {
        assert(strcmp(function, "get_recursion_context") == 0);
        acquired_slots++;
    }
}
#define barrier() do { audit_recursion_slot(rctx, __func__); \
                      __asm__ __volatile__("" ::: "memory"); } while (0)
static bool in_nmi(void) { return context_kind == 3; }
static bool in_irq(void) { return context_kind == 2; }
static bool in_softirq(void) { return context_kind == 1; }
static void preempt_disable_notrace(void)
{ preemption_depth++; disable_calls++; }
static void preempt_enable_notrace(void)
{ assert(preemption_depth > 0); preemption_depth--; enable_calls++; }
static void perf_sample_data_init(struct perf_sample_data *data, u64 address)
{ data->address = address; sample_calls++; }
static void do_perf_sw_event(u32 type, u32 event_id, u64 count,
                             struct perf_sample_data *data,
                             struct pt_regs *registers);
