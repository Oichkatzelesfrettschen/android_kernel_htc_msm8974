/*
 * include/linux/wakeup_reason.h
 *
 * Logs the IRQ that woke the AP from a suspend-to-RAM cycle, so
 * BatteryStatsService can attribute a resume to something other than
 * "unknown" in its wakeup-reason census.
 */

#ifndef _LINUX_WAKEUP_REASON_H
#define _LINUX_WAKEUP_REASON_H

#ifdef CONFIG_PM_SLEEP
void log_wakeup_reason(int irq);
#else
static inline void log_wakeup_reason(int irq) { }
#endif

#endif /* _LINUX_WAKEUP_REASON_H */
