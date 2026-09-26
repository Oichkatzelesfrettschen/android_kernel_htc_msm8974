/*
 * kernel/power/wakeup_reason.c
 *
 * Records the IRQ, or IRQs, that ended the most recent suspend-to-RAM
 * cycle and publishes them at /sys/kernel/wakeup_reasons/last_resume_reason,
 * the node userspace's BatteryStatsService reads for its wakeup-reason
 * census. A PM_SUSPEND_PREPARE notifier clears the list at the start of
 * each suspend attempt, so a stale reason from an earlier cycle never
 * carries forward; log_wakeup_reason() is the only writer and is called
 * from the platform code that resolves which wakeup source fired.
 */

#include <linux/wakeup_reason.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/spinlock.h>
#include <linux/notifier.h>
#include <linux/suspend.h>
#include <linux/kernel_stat.h>

#define MAX_WAKEUP_REASON_IRQS 32

static int wakeup_irq_list[MAX_WAKEUP_REASON_IRQS];
static int wakeup_irq_count;
static DEFINE_SPINLOCK(wakeup_reason_lock);

static struct kobject *wakeup_reason_kobj;

static ssize_t last_resume_reason_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int i;
	ssize_t off = 0;
	unsigned long flags;

	spin_lock_irqsave(&wakeup_reason_lock, flags);
	for (i = 0; i < wakeup_irq_count; i++) {
		int irq = wakeup_irq_list[i];
		struct irq_desc *desc = irq_to_desc(irq);
		const char *name = (desc && desc->action && desc->action->name)
			? desc->action->name : "";

		off += scnprintf(buf + off, PAGE_SIZE - off, "%d %s\n",
				irq, name);
		if (off >= PAGE_SIZE - 1)
			break;
	}
	spin_unlock_irqrestore(&wakeup_reason_lock, flags);

	if (off == 0)
		off = scnprintf(buf, PAGE_SIZE, "0\n");

	return off;
}

static struct kobj_attribute last_resume_reason_attr =
	__ATTR(last_resume_reason, 0444, last_resume_reason_show, NULL);

static struct attribute *wakeup_reason_attrs[] = {
	&last_resume_reason_attr.attr,
	NULL,
};

static struct attribute_group wakeup_reason_attr_group = {
	.attrs = wakeup_reason_attrs,
};

/*
 * log_wakeup_reason - record irq as (one of) the source(s) of the last
 * resume from suspend-to-RAM.
 *
 * Called from the platform power-management code at the point it already
 * resolves a sleep-mode wakeup back to an APPS IRQ number, before that IRQ
 * is marked pending and re-dispatched. Safe to call more than once per
 * resume: msm8974's MPM can latch more than one pending source in the same
 * exit_sleep pass, and every one of them is a true cause of the resume.
 */
void log_wakeup_reason(int irq)
{
	unsigned long flags;
	struct irq_desc *desc = irq_to_desc(irq);
	const char *name = (desc && desc->action && desc->action->name)
		? desc->action->name : "";

	pr_info("PM: Resume caused by IRQ %d, %s\n", irq, name);

	spin_lock_irqsave(&wakeup_reason_lock, flags);
	if (wakeup_irq_count < MAX_WAKEUP_REASON_IRQS)
		wakeup_irq_list[wakeup_irq_count++] = irq;
	else
		pr_warn("PM: wakeup_reason: more than %d wakeup IRQs, dropping %d\n",
				MAX_WAKEUP_REASON_IRQS, irq);
	spin_unlock_irqrestore(&wakeup_reason_lock, flags);
}
EXPORT_SYMBOL_GPL(log_wakeup_reason);

static int wakeup_reason_pm_event(struct notifier_block *notifier,
		unsigned long pm_event, void *unused)
{
	unsigned long flags;

	if (pm_event != PM_SUSPEND_PREPARE)
		return NOTIFY_DONE;

	spin_lock_irqsave(&wakeup_reason_lock, flags);
	wakeup_irq_count = 0;
	spin_unlock_irqrestore(&wakeup_reason_lock, flags);

	return NOTIFY_DONE;
}

static struct notifier_block wakeup_reason_pm_notifier_block = {
	.notifier_call = wakeup_reason_pm_event,
};

static int __init wakeup_reason_init(void)
{
	int ret;

	ret = register_pm_notifier(&wakeup_reason_pm_notifier_block);
	if (ret)
		pr_warn("wakeup_reason: failed to register PM notifier: %d\n",
				ret);

	wakeup_reason_kobj = kobject_create_and_add("wakeup_reasons",
			kernel_kobj);
	if (!wakeup_reason_kobj) {
		pr_warn("wakeup_reason: failed to create sysfs kobject\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(wakeup_reason_kobj, &wakeup_reason_attr_group);
	if (ret) {
		pr_warn("wakeup_reason: failed to create sysfs group: %d\n",
				ret);
		kobject_put(wakeup_reason_kobj);
		wakeup_reason_kobj = NULL;
	}

	return ret;
}
late_initcall(wakeup_reason_init);
