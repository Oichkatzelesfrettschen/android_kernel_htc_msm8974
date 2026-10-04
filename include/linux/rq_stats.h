/* Copyright (c) 2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

struct rq_data {
	unsigned int rq_avg;
	unsigned long rq_poll_jiffies;
	unsigned long def_timer_jiffies;
	unsigned long rq_poll_last_jiffy;
	unsigned long rq_poll_total_jiffies;
	unsigned long def_timer_last_jiffy;
	unsigned int def_interval;
	unsigned int hotplug_disabled;
	int64_t def_start_time;
	struct attribute_group *attr_group;
	struct kobject *kobj;
	struct work_struct def_timer_work;
	int init;
};

extern spinlock_t rq_lock;
extern struct rq_data rq_info;
extern struct workqueue_struct *rq_wq;

/*
 * Reasons carried in rq_data.hotplug_disabled. The userspace hotplug daemon
 * (mpdecision) reads rq-stats/hotplug_disable and leaves the CPU online mask
 * alone while it reads 1; each holder sets and clears only its own bit.
 */
#define RQ_HOTPLUG_DISABLE_SUSPEND	(1U << 0)
#define RQ_HOTPLUG_DISABLE_INPUT	(1U << 1)

#ifdef CONFIG_MSM_RUN_QUEUE_STATS
void rq_hotplug_disable_set(unsigned int reason, bool disable);
#else
static inline void rq_hotplug_disable_set(unsigned int reason, bool disable)
{
}
#endif
