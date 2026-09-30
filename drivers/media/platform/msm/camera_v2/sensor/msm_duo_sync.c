/*
 * Frame-start phase lock of the OV2722 depth subcamera to the main camera.
 *
 * The two rear sensors share no sync line, so each free-runs on its own
 * frame length and their frame starts beat against each other. This loop
 * reads both VFEs' start-of-frame times, and on every subcamera frame start
 * sets the subcamera frame length (OV2722 0x380e/0x380f) to the main
 * camera's period in subcamera lines plus a proportional-integral correction
 * of the phase error. The frame length reaches the sensor two ways: the
 * daemon's own exposure tables carry it and are rewritten in flight, and a
 * work item writes it whenever the loop changes it between tables. The
 * sensor stops delivering frames when exposure passes the frame length less
 * 4 lines, and with its analog gain at maximum the daemon asks for about
 * 1137 lines, more than a frame at the main camera's 30.05 fps holds; while
 * engaged, exposure is therefore clamped to the frame-length floor less 4
 * lines in both paths. The loop engages only while both VFEs deliver frame
 * starts; when the main camera stops, the daemon's last frame length and
 * exposure are restored.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/math64.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <media/msm_cam_sensor.h>
#include "msm_duo_sync.h"

/* VFE interface carrying CSID 1, the subcamera (msm_ispif.c). */
extern int g_subcam_vfe_intf;

#define DUO_VFES 2
#define DUO_DUMP_MAX 16
/* A VFE whose last frame start is older than this many periods is idle. */
#define DUO_IDLE_PERIODS 3
/* Frame starts closer than this or farther apart restart the estimate. */
#define DUO_PERIOD_MIN_NS 5000000
#define DUO_PERIOD_MAX_NS 200000000
/* OV2722: exposure must end 4 lines before the frame does. */
#define DUO_EXP_MARGIN 4
#define DUO_FL_MAX 0x7fff

struct duo_sync {
	spinlock_t lock;
	s64 sof_ns[DUO_VFES];
	u32 period_ns[DUO_VFES];

	/* Configuration (sysfs). */
	bool enable;
	u32 fl_override;
	s32 offset_ns;
	u32 fl_min;
	u32 trim_max;
	u32 kp_div;
	u32 ki_div;

	/* Loop state. */
	bool active;
	bool streaming;
	u32 line_ps;
	s32 integ_q8;
	u32 frac_q8;
	u32 fl_cmd;
	u32 fl_written;
	u32 fl_daemon;
	u32 exp_raw;
	u32 exp_lines;
	s32 phase_ns;
	u32 lock_frames;
	bool restore;

	/* Telemetry. */
	u32 sub_sofs;
	u32 daemon_fl_tables;
	u32 seq_writes;
	u32 servo_writes;
	u32 servo_skips;
	u32 write_errors;
	u32 dump_n;
	u32 dump_type;
	u16 dump_addr[DUO_DUMP_MAX];
	u16 dump_data[DUO_DUMP_MAX];
};

static struct duo_sync ds = {
	.lock = __SPIN_LOCK_UNLOCKED(ds.lock),
	.fl_min = 1100,
	.trim_max = 24,
	.kp_div = 8,
	.ki_div = 64,
};

static duo_sync_write_fl_t duo_write_fl;

static void duo_sync_work_fn(struct work_struct *work);
static DECLARE_WORK(duo_sync_work, duo_sync_work_fn);

static s64 tv_ns(const struct timeval *tv)
{
	return (s64)tv->tv_sec * NSEC_PER_SEC + (s64)tv->tv_usec * NSEC_PER_USEC;
}

static void duo_reset_loop(void)
{
	ds.active = false;
	ds.integ_q8 = 0;
	ds.frac_q8 = 0;
	ds.lock_frames = 0;
	if (ds.fl_cmd) {
		ds.fl_cmd = 0;
		ds.restore = ds.fl_daemon != 0;
	}
}

/* Frame length in lines, Q8, for a duration at the learned line time. */
static s64 duo_lines_q8(s64 ns)
{
	return div_s64(ns * 1000 * 256, ds.line_ps);
}

static void duo_run_loop(s64 now, int svfe, int mvfe)
{
	u32 tm = ds.period_ns[mvfe];
	s64 e;
	s64 target_q8;
	s32 corr_q8;
	u32 fl;

	/* Wrap the sub-to-main frame start distance into (-T/2, T/2]. */
	e = now - ds.sof_ns[mvfe] - ds.offset_ns;
	e = e - div_s64(e, tm) * tm;
	if (e < 0)
		e += tm;
	if (e > tm / 2)
		e -= tm;
	ds.phase_ns = (s32)e;
	if (e > -1000000 && e < 1000000)
		ds.lock_frames++;
	else
		ds.lock_frames = 0;

	/* A late subcamera frame start shortens the next subcamera frame. */
	corr_q8 = -(s32)div_s64(duo_lines_q8(e), ds.kp_div);
	ds.integ_q8 -= (s32)div_s64(duo_lines_q8(e), ds.ki_div);
	ds.integ_q8 = clamp_t(s32, ds.integ_q8, -(s32)ds.trim_max * 256,
		(s32)ds.trim_max * 256);
	corr_q8 += ds.integ_q8;
	corr_q8 = clamp_t(s32, corr_q8, -(s32)ds.trim_max * 256,
		(s32)ds.trim_max * 256);

	target_q8 = duo_lines_q8(tm) + corr_q8 + ds.frac_q8;
	if (target_q8 < 0)
		target_q8 = 0;
	fl = (u32)(target_q8 >> 8);
	ds.frac_q8 = (u32)(target_q8 & 0xff);

	ds.fl_cmd = clamp_t(u32, fl, ds.fl_min, DUO_FL_MAX);
}

/* Exposure register value (lines x 16) the sensor may carry while engaged. */
static u32 duo_exp_limit(void)
{
	u32 floor = ds.fl_override ? ds.fl_override : ds.fl_min;

	return (floor - DUO_EXP_MARGIN) << 4;
}

void duo_sync_sof(int vfe_id, const struct timeval *ts)
{
	unsigned long flags;
	s64 now = tv_ns(ts);
	s64 d;
	int svfe, mvfe;
	bool main_live;
	bool kick = false;

	if (vfe_id < 0 || vfe_id >= DUO_VFES)
		return;

	spin_lock_irqsave(&ds.lock, flags);
	d = now - ds.sof_ns[vfe_id];
	if (ds.sof_ns[vfe_id] && d >= DUO_PERIOD_MIN_NS &&
			d <= DUO_PERIOD_MAX_NS) {
		if (ds.period_ns[vfe_id])
			ds.period_ns[vfe_id] +=
				(s32)(d - ds.period_ns[vfe_id]) / 8;
		else
			ds.period_ns[vfe_id] = (u32)d;
	} else {
		ds.period_ns[vfe_id] = 0;
	}
	ds.sof_ns[vfe_id] = now;

	svfe = g_subcam_vfe_intf;
	if (vfe_id != svfe || svfe < 0 || svfe >= DUO_VFES || !ds.streaming)
		goto out;
	mvfe = DUO_VFES - 1 - svfe;
	ds.sub_sofs++;

	main_live = ds.period_ns[mvfe] && ds.period_ns[svfe] &&
		now - ds.sof_ns[mvfe] <
			(s64)DUO_IDLE_PERIODS * ds.period_ns[mvfe];

	if (ds.fl_override) {
		ds.active = true;
		ds.fl_cmd = ds.fl_override;
	} else if (ds.enable && main_live && ds.line_ps) {
		ds.active = true;
		duo_run_loop(now, svfe, mvfe);
	} else {
		/*
		 * Released: the daemon's frame length is on the sensor, so the
		 * subcamera period over that length is its line time.
		 */
		if (ds.active)
			duo_reset_loop();
		if (ds.fl_daemon && ds.period_ns[svfe])
			ds.line_ps = (u32)div_u64((u64)ds.period_ns[svfe] * 1000,
				ds.fl_daemon);
	}

	kick = (ds.fl_cmd && ds.fl_cmd != ds.fl_written) || ds.restore;
out:
	spin_unlock_irqrestore(&ds.lock, flags);
	if (kick)
		schedule_work(&duo_sync_work);
}

static void duo_sync_work_fn(struct work_struct *work)
{
	unsigned long flags;
	u32 fl;
	bool restore;
	int rc;

	u32 exp;

	spin_lock_irqsave(&ds.lock, flags);
	restore = ds.restore && !ds.fl_cmd;
	fl = restore ? ds.fl_daemon : ds.fl_cmd;
	exp = restore ? ds.exp_raw : min(ds.exp_raw, duo_exp_limit());
	spin_unlock_irqrestore(&ds.lock, flags);

	if (!fl || !duo_write_fl)
		return;
	rc = duo_write_fl((u16)fl, exp);

	spin_lock_irqsave(&ds.lock, flags);
	if (rc == -EBUSY) {
		ds.servo_skips++;
	} else if (rc) {
		ds.write_errors++;
	} else {
		ds.servo_writes++;
		ds.fl_written = fl;
		if (restore)
			ds.restore = false;
	}
	spin_unlock_irqrestore(&ds.lock, flags);
}

void duo_sync_filter_table(struct msm_camera_i2c_reg_setting *setting)
{
	struct msm_camera_i2c_reg_array *r = setting->reg_setting;
	unsigned long flags;
	bool byte = setting->data_type == MSM_CAMERA_I2C_BYTE_DATA;
	bool word = setting->data_type == MSM_CAMERA_I2C_WORD_DATA;
	int fl_hi = -1, fl_lo = -1, fl_word = -1;
	int exp_idx[3] = {-1, -1, -1};
	u32 exp = 0;
	bool has_exp = false;
	u32 fl;
	int i;

	if (!byte && !word)
		return;

	spin_lock_irqsave(&ds.lock, flags);
	for (i = 0; i < setting->size; i++) {
		u16 a = r[i].reg_addr;
		u16 v = r[i].reg_data;

		if (a == 0x0100 && byte) {
			ds.streaming = v & 1;
			if (!ds.streaming) {
				duo_reset_loop();
				ds.restore = false;
			}
		} else if (a == 0x380e && byte) {
			fl_hi = i;
		} else if (a == 0x380f && byte) {
			fl_lo = i;
		} else if (a == 0x380e && word) {
			fl_word = i;
		} else if (byte && a >= 0x3500 && a <= 0x3502) {
			/* Exposure {0x3500[3:0], 0x3501, 0x3502} in 1/16 lines. */
			exp |= (u32)(v & 0xff) << (8 * (0x3502 - a));
			exp_idx[a - 0x3500] = i;
			has_exp = true;
		}
	}

	if (has_exp) {
		ds.exp_raw = exp & 0xfffff;
		ds.exp_lines = ds.exp_raw >> 4;
		if (ds.fl_cmd && ds.exp_raw > duo_exp_limit()) {
			exp = duo_exp_limit();
			for (i = 0; i < 3; i++)
				if (exp_idx[i] >= 0)
					r[exp_idx[i]].reg_data =
						(exp >> (8 * (2 - i))) & 0xff;
		}
	}

	if (fl_hi >= 0 || fl_word >= 0) {
		ds.daemon_fl_tables++;
		ds.dump_type = setting->data_type;
		ds.dump_n = min_t(u32, setting->size, DUO_DUMP_MAX);
		for (i = 0; i < ds.dump_n; i++) {
			ds.dump_addr[i] = r[i].reg_addr;
			ds.dump_data[i] = r[i].reg_data;
		}
		if (fl_word >= 0)
			ds.fl_daemon = r[fl_word].reg_data;
		else if (fl_lo >= 0)
			ds.fl_daemon = (r[fl_hi].reg_data & 0xff) << 8 |
				(r[fl_lo].reg_data & 0xff);

		if (ds.fl_cmd) {
			fl = ds.fl_cmd;
			if (fl_word >= 0) {
				r[fl_word].reg_data = fl;
			} else if (fl_lo >= 0) {
				r[fl_hi].reg_data = fl >> 8;
				r[fl_lo].reg_data = fl & 0xff;
			}
			ds.fl_written = fl;
		} else {
			ds.fl_written = ds.fl_daemon;
		}
	}
	spin_unlock_irqrestore(&ds.lock, flags);
}

void duo_sync_count_seq_write(void)
{
	unsigned long flags;

	spin_lock_irqsave(&ds.lock, flags);
	ds.seq_writes++;
	spin_unlock_irqrestore(&ds.lock, flags);
}

void duo_sync_stream_off(void)
{
	unsigned long flags;

	spin_lock_irqsave(&ds.lock, flags);
	ds.streaming = false;
	duo_reset_loop();
	ds.restore = false;
	ds.fl_written = 0;
	spin_unlock_irqrestore(&ds.lock, flags);
	/*
	 * A work item still queued finds nothing to write and returns; it only
	 * trylocks the sensor mutex this caller may hold.
	 */
}

void duo_sync_register_writer(duo_sync_write_fl_t write_fl)
{
	duo_write_fl = write_fl;
}

static ssize_t duo_status_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf)
{
	unsigned long flags;
	struct duo_sync s;
	ssize_t n;
	u32 i;

	spin_lock_irqsave(&ds.lock, flags);
	s = ds;
	spin_unlock_irqrestore(&ds.lock, flags);

	n = scnprintf(buf, PAGE_SIZE,
		"enable=%d active=%d streaming=%d sub_vfe=%d override=%u\n"
		"period0_ns=%u period1_ns=%u line_ps=%u offset_ns=%d\n"
		"phase_ns=%d lock_frames=%u fl_cmd=%u fl_written=%u fl_daemon=%u exp_lines=%u exp_limit=%u\n"
		"integ_q8=%d fl_min=%u trim_max=%u kp_div=%u ki_div=%u\n"
		"sub_sofs=%u daemon_fl_tables=%u seq_writes=%u servo_writes=%u servo_skips=%u write_errors=%u\n"
		"dump_type=%u",
		s.enable, s.active, s.streaming, g_subcam_vfe_intf,
		s.fl_override, s.period_ns[0], s.period_ns[1], s.line_ps,
		s.offset_ns, s.phase_ns, s.lock_frames, s.fl_cmd,
		s.fl_written, s.fl_daemon, s.exp_lines,
		s.fl_cmd ? (s.fl_override ? s.fl_override : s.fl_min) - DUO_EXP_MARGIN : 0,
		s.integ_q8, s.fl_min,
		s.trim_max, s.kp_div, s.ki_div, s.sub_sofs,
		s.daemon_fl_tables, s.seq_writes, s.servo_writes,
		s.servo_skips, s.write_errors, s.dump_type);
	for (i = 0; i < s.dump_n; i++)
		n += scnprintf(buf + n, PAGE_SIZE - n, " 0x%04x=0x%04x",
			s.dump_addr[i], s.dump_data[i]);
	n += scnprintf(buf + n, PAGE_SIZE - n, "\n");
	return n;
}

#define DUO_RW_ATTR(name, field, type, check)				\
static ssize_t duo_##name##_show(struct kobject *kobj,			\
	struct kobj_attribute *attr, char *buf)				\
{									\
	return scnprintf(buf, PAGE_SIZE, "%lld\n", (long long)ds.field);\
}									\
static ssize_t duo_##name##_store(struct kobject *kobj,			\
	struct kobj_attribute *attr, const char *buf, size_t count)	\
{									\
	unsigned long flags;						\
	long long v;							\
	int rc = kstrtoll(buf, 0, &v);					\
									\
	if (rc)								\
		return rc;						\
	if (!(check))							\
		return -EINVAL;						\
	spin_lock_irqsave(&ds.lock, flags);				\
	ds.field = (type)v;						\
	duo_reset_loop();						\
	spin_unlock_irqrestore(&ds.lock, flags);			\
	schedule_work(&duo_sync_work);					\
	return count;							\
}									\
static struct kobj_attribute duo_##name##_attr =			\
	__ATTR(duo_sync_##name, 0644, duo_##name##_show, duo_##name##_store)

DUO_RW_ATTR(enable, enable, bool, v == 0 || v == 1);
DUO_RW_ATTR(fl_override, fl_override, u32, v == 0 || (v >= 1000 && v <= DUO_FL_MAX));
DUO_RW_ATTR(offset_ns, offset_ns, s32, v > -50000000LL && v < 50000000LL);
DUO_RW_ATTR(fl_min, fl_min, u32, v >= 1000 && v <= DUO_FL_MAX);
DUO_RW_ATTR(trim_max, trim_max, u32, v >= 1 && v <= 256);
DUO_RW_ATTR(kp_div, kp_div, u32, v >= 1 && v <= 1024);
DUO_RW_ATTR(ki_div, ki_div, u32, v >= 1 && v <= 65536);

static struct kobj_attribute duo_status_attr =
	__ATTR(duo_sync_status, 0444, duo_status_show, NULL);

static struct attribute *duo_attrs[] = {
	&duo_status_attr.attr,
	&duo_enable_attr.attr,
	&duo_fl_override_attr.attr,
	&duo_offset_ns_attr.attr,
	&duo_fl_min_attr.attr,
	&duo_trim_max_attr.attr,
	&duo_kp_div_attr.attr,
	&duo_ki_div_attr.attr,
	NULL,
};

static const struct attribute_group duo_attr_group = {
	.attrs = duo_attrs,
};

int duo_sync_sysfs_init(struct kobject *kobj)
{
	return sysfs_create_group(kobj, &duo_attr_group);
}
