#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#define CPU_DYING 8
#define CPU_TASKS_FROZEN 16
#define NOTIFY_DONE 0
#define NOTIFY_OK 1
#define IRQ_WORK_BUSY 2
#define __get_cpu_var(variable) (variable)
#define BUG_ON(value) assert(!(value))
#define WARN_ON_ONCE(value) warning(value)
#define llist_entry(pointer, type, member) ((type *)((char *)(pointer)-offsetof(type, member)))
struct notifier_block { int unused; };
struct llist_node { struct llist_node *next; };
struct llist_head { struct llist_node *first; };
struct irq_work { unsigned long flags; struct llist_node llnode; void (*func)(struct irq_work *); };
static struct llist_head irq_work_list;
static bool irq_disabled, hardirq;
static unsigned callbacks;
static bool warning(bool value) { assert(!value); return value; }
static bool irqs_disabled(void) { return irq_disabled; }
static bool in_irq(void) { return hardirq; }
static unsigned smp_processor_id(void) { return 3; }
static bool llist_empty(struct llist_head *head) { return head->first==NULL; }
static struct llist_node *llist_del_all(struct llist_head *head)
{ struct llist_node *first=head->first; head->first=NULL; return first; }
static struct llist_node *llist_next(struct llist_node *node) { return node->next; }
static unsigned long cmpxchg(unsigned long *value, unsigned long old, unsigned long replacement)
{ unsigned long previous=*value; if (previous==old) *value=replacement; return previous; }
static void cpu_relax(void) { assert(!"pending work survives target quiescence"); }
static void pending_callback(struct irq_work *work)
{ assert(work->flags==IRQ_WORK_BUSY && irq_disabled); callbacks++; }
