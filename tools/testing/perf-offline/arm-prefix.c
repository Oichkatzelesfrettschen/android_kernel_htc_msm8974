#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
typedef uint32_t u32;
typedef uint8_t u8;
typedef unsigned long long u64;
#define PMU_CODES_SIZE 64
#define PERF_EVENT_STATE_OFF (-1)
#define PERF_EF_START 1
#define PERF_EF_RELOAD 2
#define PERF_EF_UPDATE 4
#define PERF_HES_STOPPED 1
#define PERF_HES_UPTODATE 2
#define WARN_ON(value) assert(!(value))
static bool warning(bool condition) { assert(!condition); return condition; }
#define WARN_ON_ONCE(value) warning(value)
#define pr_err(...) ((void)0)
#define raw_spin_lock_irqsave(lock, flags) do { assert(!*(lock)); *(lock)=true; (flags)=1; } while (0)
#define raw_spin_unlock_irqrestore(lock, flags) do { assert(*(lock) && (flags)==1); *(lock)=false; } while (0)
struct perf_event;
struct pmu { unsigned unused; };
struct hw_perf_event { int idx, state; };
struct pmu_hw_events { struct perf_event *events[4]; unsigned long used_mask[1]; };
struct perf_event {
    struct pmu *pmu;
    struct hw_perf_event hw;
    struct { u64 config; bool constraint_duplicate; } attr;
    int cpu, state;
};
struct arm_pmu {
    struct pmu pmu;
    struct pmu_hw_events *(*get_hw_events)(void);
    int (*test_set_event_constraints)(struct perf_event *);
    int (*clear_event_constraints)(struct perf_event *);
    int (*get_event_idx)(struct pmu_hw_events *, struct hw_perf_event *);
    void (*disable)(struct hw_perf_event *, int);
};
static struct {
    u64 pmu_bitmap;
    u8 codes[PMU_CODES_SIZE];
    unsigned users[PMU_CODES_SIZE];
    bool lock;
} l2_pmu_constraints;
static struct pmu_hw_events hardware;
static bool fail_allocation;
static unsigned stop_calls, start_calls, page_updates;
#define to_arm_pmu(pointer) ((struct arm_pmu *)(pointer))
static struct pmu_hw_events *get_hardware(void) { return &hardware; }
static void clear_bit(int index, unsigned long *mask) { assert(index>=0 && index<4); *mask &= ~(1UL<<index); }
static int allocate_slot(struct pmu_hw_events *events, struct hw_perf_event *hw)
{
    (void)hw;
    if (fail_allocation) return -ENOSPC;
    for (int index=0; index<4; index++) {
        if (!(events->used_mask[0] & (1UL<<index))) {
            events->used_mask[0] |= 1UL<<index;
            return index;
        }
    }
    return -ENOSPC;
}
static void disable_counter(struct hw_perf_event *hw, int index) { assert(hw && index>=0 && index<4); }
static void perf_pmu_disable(struct pmu *pmu) { assert(pmu); }
static void perf_pmu_enable(struct pmu *pmu) { assert(pmu); }
static void perf_event_update_userpage(struct perf_event *event) { assert(event); page_updates++; }
static void armpmu_stop(struct perf_event *event, int flags)
{ assert(flags==PERF_EF_UPDATE); assert(event->hw.idx>=0); stop_calls++; }
static void armpmu_start(struct perf_event *event, int flags)
{ assert(flags==PERF_EF_RELOAD); assert(event->hw.idx>=0); start_calls++; }

#define __get_cpu_var(variable) (variable)
#define KRAIT_EVENT_MASK 0xfffff
static u64 pmu_bitmap;
