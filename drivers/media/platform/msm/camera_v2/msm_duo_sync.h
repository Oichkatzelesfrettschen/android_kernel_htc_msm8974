/*
 * Frame-start phase lock of the OV2722 depth subcamera to the main camera.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 */
#ifndef MSM_DUO_SYNC_H
#define MSM_DUO_SYNC_H

#include <linux/time.h>
#include <linux/types.h>

struct kobject;
struct msm_camera_i2c_reg_setting;

/*
 * Writes a frame length in lines and, when exp is nonzero, the exposure
 * register value (lines x 16) to the subcamera in one group hold; returns 0
 * on success and -EBUSY when the sensor mutex is held.
 */
typedef int (*duo_sync_write_fl_t)(u16 fl, u32 exp);

#ifdef CONFIG_OV2722
void duo_sync_sof(int vfe_id, const struct timeval *ts);
void duo_sync_filter_table(struct msm_camera_i2c_reg_setting *setting);
void duo_sync_count_seq_write(void);
void duo_sync_stream_off(void);
void duo_sync_register_writer(duo_sync_write_fl_t write_fl);
int duo_sync_sysfs_init(struct kobject *kobj);
#else
static inline void duo_sync_sof(int vfe_id, const struct timeval *ts) {}
#endif

#endif
