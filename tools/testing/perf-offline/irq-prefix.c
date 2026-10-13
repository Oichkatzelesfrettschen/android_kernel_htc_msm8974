#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#define __cpuinit
#define CPU_DYING 8
#define CPU_STARTING 10
#define CPU_DOWN_PREPARE 5
#define CPU_DOWN_FAILED 6
#define CPU_TASKS_FROZEN 16
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
#define __get_cpu_var(variable) (variable)
struct notifier_block { unsigned unused; };
struct platform_device { unsigned unused; };
struct arm_pmu {
    unsigned active_irqs;
    void (*free_pmu_irq)(int);
    void (*save_pm_registers)(void *);
    void (*restore_pm_registers)(void *);
    void (*reset)(void *);
    struct platform_device *plat_device;
};
static struct arm_pmu *cpu_pmu;
static unsigned from_idle, saves, resets, restores, enables, disables;
static bool irq_enabled;
static bool cpumask_test_cpu(unsigned cpu, const unsigned *mask)
{ return (*mask & (1U<<cpu)) != 0; }
static void multicore_free_irq(int irq) { (void)irq; }
static int platform_get_irq(struct platform_device *device, unsigned resource)
{ assert(device && resource==0); return 42; }
static void enable_irq_callback(void *argument)
{ assert(*(int *)argument==42 && !irq_enabled); irq_enabled=true; enables++; }
static void disable_irq_callback(void *argument)
{ assert(*(int *)argument==42 && irq_enabled); irq_enabled=false; disables++; }
static void save_registers(void *cpu)
{ assert((uintptr_t)cpu==3); saves++; }
static void reset_registers(void *unused)
{ assert(!unused && !irq_enabled); resets++; }
static void restore_registers(void *cpu)
{ assert((uintptr_t)cpu==3 && resets>restores); restores++; }
