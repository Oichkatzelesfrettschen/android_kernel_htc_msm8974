#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define CPU_DYING 8
#define CPU_TASKS_FROZEN 16
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
#define __weak
#define BUG_ON(condition) assert(!(condition))
#define WARN_ON_ONCE(condition) record_warning(condition)
#define __get_cpu_var(variable) (variable ## s[executing_cpu])
#define llist_entry(pointer, type, member) \
    ((type *)((char *)(pointer) - offsetof(type, member)))

struct notifier_block { int unused; };
struct llist_node { struct llist_node *next; };
struct llist_head { struct llist_node *first; };
struct irq_work {
    unsigned long flags;
    struct llist_node llnode;
    void (*func)(struct irq_work *);
};

static struct llist_head irq_work_lists[4];
static unsigned executing_cpu, preempt_depth, callback_depth, warning_count;
static bool interrupts_disabled, hardirq_context;

static bool record_warning(bool condition)
{
    warning_count += condition;
    return condition;
}

static bool irqs_disabled(void) { return interrupts_disabled; }
static bool in_irq(void) { return hardirq_context; }
static unsigned smp_processor_id(void) { return executing_cpu; }
static void preempt_disable(void) { preempt_depth++; }
static void preempt_enable(void) { assert(preempt_depth); preempt_depth--; }
static void cpu_relax(void) { assert(!"fixture compare-exchange retry"); }

static bool llist_empty(struct llist_head *head) { return head->first == NULL; }
static struct llist_node *llist_next(struct llist_node *node) { return node->next; }

static bool llist_add(struct llist_node *node, struct llist_head *head)
{
    bool empty = head->first == NULL;
    node->next = head->first;
    head->first = node;
    return empty;
}

static struct llist_node *llist_del_all(struct llist_head *head)
{
    struct llist_node *first = head->first;
    head->first = NULL;
    return first;
}

static unsigned long cmpxchg(unsigned long *address, unsigned long expected,
                             unsigned long replacement)
{
    unsigned long previous = *address;
    if (previous == expected)
        *address = replacement;
    return previous;
}
