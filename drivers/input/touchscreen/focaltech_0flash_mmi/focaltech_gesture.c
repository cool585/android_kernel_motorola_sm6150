/*
 *
 * FocalTech TouchScreen driver.
 *
 * Copyright (c) 2012-2019, Focaltech Ltd. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*****************************************************************************
*
* File Name: focaltech_gestrue.c
*
* Author: Focaltech Driver Team
*
* Created: 2016-08-08
*
* Abstract:
*
* Reference:
*
*****************************************************************************/

/*****************************************************************************
* 1.Included header files
*****************************************************************************/
#include "focaltech_core.h"
#include <linux/panel_notifier.h>
#if FTS_GESTURE_EN
/******************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#define KEY_GESTURE_U                           KEY_U
#define KEY_GESTURE_UP                          KEY_UP
#define KEY_GESTURE_DOWN                        KEY_DOWN
#define KEY_GESTURE_LEFT                        KEY_LEFT
#define KEY_GESTURE_RIGHT                       KEY_RIGHT
#define KEY_GESTURE_O                           KEY_O
#define KEY_GESTURE_E                           KEY_E
#define KEY_GESTURE_M                           KEY_M
#define KEY_GESTURE_L                           KEY_L
#define KEY_GESTURE_W                           KEY_W
#define KEY_GESTURE_S                           KEY_S
#define KEY_GESTURE_V                           KEY_V
#define KEY_GESTURE_C                           KEY_C
#define KEY_GESTURE_Z                           KEY_Z

#define GESTURE_LEFT                            0x20
#define GESTURE_RIGHT                           0x21
#define GESTURE_UP                              0x22
#define GESTURE_DOWN                            0x23
#define GESTURE_DOUBLECLICK                     0x24
#define GESTURE_O                               0x30
#define GESTURE_W                               0x31
#define GESTURE_M                               0x32
#define GESTURE_E                               0x33
#define GESTURE_L                               0x44
#define GESTURE_S                               0x46
#define GESTURE_V                               0x54
#define GESTURE_Z                               0x41
#define GESTURE_C                               0x34

#ifdef FOCALTECH_SENSOR_EN
#define REPORT_MAX_COUNT 10000
#endif

/* Double tap detection resources */
#define DT2W_FEATHER          200   /* max pixel movement between taps or during tap */
#define DT2W_TAP_MAX_DURATION 300   /* max duration of a single tap down in ms */
#define DT2W_GAP_MIN          40    /* min gap between tap1 UP and tap2 DOWN in ms (debounce) */
#define DT2W_GAP_MAX          450   /* max gap between tap1 UP and tap2 DOWN in ms */
#define DT2W_TOTAL_MAX_TIME   650   /* max total time from tap1 DOWN to tap2 DOWN in ms */

enum dt2w_state {
    DT2W_STATE_IDLE = 0,
    DT2W_STATE_TAP1_DOWN,
    DT2W_STATE_TAP1_UP,
    DT2W_STATE_REJECT,
};

static enum dt2w_state dt_state = DT2W_STATE_IDLE;
static unsigned long long tap1_down_time = 0;
static unsigned long long tap1_up_time = 0;
static int tap1_x = 0;
static int tap1_y = 0;

/*****************************************************************************
* Private enumerations, structures and unions using typedef
*****************************************************************************/
/*
* gesture_id    - mean which gesture is recognised
* point_num     - points number of this gesture
* coordinate_x  - All gesture point x coordinate
* coordinate_y  - All gesture point y coordinate
* mode          - gesture enable/disable, need enable by host
*               - 1:enable gesture function(default)  0:disable
* active        - gesture work flag,
*                 always set 1 when suspend, set 0 when resume
*/
struct fts_gesture_st {
    u8 gesture_id;
    u8 point_num;
    u16 coordinate_x[FTS_GESTURE_POINTS_MAX];
    u16 coordinate_y[FTS_GESTURE_POINTS_MAX];
    u8 mode;
    u8 active;
};

/*****************************************************************************
* Static variables
*****************************************************************************/
static struct fts_gesture_st fts_gesture_data;
#ifdef FOCALTECH_SENSOR_EN
#ifdef CONFIG_HAS_WAKELOCK
static struct wake_lock gesture_wakelock;
#else
static struct wakeup_source gesture_wakelock;
#endif
static struct sensors_classdev __maybe_unused sensors_touch_cdev = {
    .name = "dt-gesture",
    .vendor = "Focaltech",
    .version = 1,
    .type = SENSOR_TYPE_MOTO_DOUBLE_TAP,
    .max_range = "5.0",
    .resolution = "5.0",
    .sensor_power = "1",
    .min_delay = 0,
    .max_delay = 0,
    /* WAKE_UP & ON_CHANGE */
    .flags = 1 | 2,
    .fifo_reserved_event_count = 0,
    .fifo_max_event_count = 0,
    .enabled = 0,
    .delay_msec = 200,
    .sensors_enable = NULL,
    .sensors_poll_delay = NULL,
};
#endif

/*****************************************************************************
* Global variable or extern global variabls/functions
*****************************************************************************/

/*****************************************************************************
* Static function prototypes
*****************************************************************************/
static ssize_t fts_gesture_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    u8 val = 0;
    struct input_dev *input_dev = fts_data->input_dev;

    mutex_lock(&input_dev->mutex);
    fts_read_reg(FTS_REG_GESTURE_EN, &val);
    count = snprintf(buf, PAGE_SIZE, "Gesture Mode:%s\n",
                     fts_gesture_data.mode ? "On" : "Off");
    count += snprintf(buf + count, PAGE_SIZE, "Reg(0xD0)=%d\n", val);
    mutex_unlock(&input_dev->mutex);

    return count;
}

static ssize_t fts_gesture_store(
    struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    struct input_dev *input_dev = fts_data->input_dev;
    mutex_lock(&input_dev->mutex);
    if (FTS_SYSFS_ECHO_ON(buf)) {
        FTS_DEBUG("enable gesture");
        fts_gesture_data.mode = ENABLE;
    } else if (FTS_SYSFS_ECHO_OFF(buf)) {
        FTS_DEBUG("disable gesture");
        fts_gesture_data.mode = DISABLE;
    }
    mutex_unlock(&input_dev->mutex);

    return count;
}

static ssize_t fts_gesture_buf_show(
    struct device *dev, struct device_attribute *attr, char *buf)
{
    int count = 0;
    int i = 0;
    struct input_dev *input_dev = fts_data->input_dev;
    struct fts_gesture_st *gesture = &fts_gesture_data;

    mutex_lock(&input_dev->mutex);
    count = snprintf(buf, PAGE_SIZE, "Gesture ID:%d\n", gesture->gesture_id);
    count += snprintf(buf + count, PAGE_SIZE, "Gesture PointNum:%d\n",
                      gesture->point_num);
    count += snprintf(buf + count, PAGE_SIZE, "Gesture Points Buffer:\n");

    /* save point data,max:6 */
    for (i = 0; i < FTS_GESTURE_POINTS_MAX; i++) {
        count += snprintf(buf + count, PAGE_SIZE, "%3d(%4d,%4d) ", i,
                          gesture->coordinate_x[i], gesture->coordinate_y[i]);
        if ((i + 1) % 4 == 0)
            count += snprintf(buf + count, PAGE_SIZE, "\n");
    }
    count += snprintf(buf + count, PAGE_SIZE, "\n");
    mutex_unlock(&input_dev->mutex);

    return count;
}

static ssize_t fts_gesture_buf_store(
    struct device *dev,
    struct device_attribute *attr, const char *buf, size_t count)
{
    return -EPERM;
}


/* sysfs gesture node
 *   read example: cat  fts_gesture_mode       ---read gesture mode
 *   write example:echo 1 > fts_gesture_mode   --- write gesture mode to 1
 *
 */
static DEVICE_ATTR(fts_gesture_mode, S_IRUGO | S_IWUSR, fts_gesture_show,
                   fts_gesture_store);
/*
 *   read example: cat fts_gesture_buf        --- read gesture buf
 */
static DEVICE_ATTR(fts_gesture_buf, S_IRUGO | S_IWUSR,
                   fts_gesture_buf_show, fts_gesture_buf_store);

static struct attribute *fts_gesture_mode_attrs[] = {
    &dev_attr_fts_gesture_mode.attr,
    &dev_attr_fts_gesture_buf.attr,
    NULL,
};

static struct attribute_group fts_gesture_group = {
    .attrs = fts_gesture_mode_attrs,
};

int fts_create_gesture_sysfs(struct device *dev)
{
    int ret = 0;

    ret = sysfs_create_group(&dev->kobj, &fts_gesture_group);
    if (ret) {
        FTS_ERROR("gesture sys node create fail");
        sysfs_remove_group(&dev->kobj, &fts_gesture_group);
        return ret;
    }

    return 0;
}

void fts_gesture_report(struct input_dev *input_dev, int gesture_id)
{
    int gesture;
#ifdef FOCALTECH_SENSOR_EN
    static int report_cnt = 0;
#endif

    FTS_DEBUG("gesture_id:0x%x", gesture_id);
    switch (gesture_id) {
    case GESTURE_LEFT:
        gesture = KEY_GESTURE_LEFT;
        break;
    case GESTURE_RIGHT:
        gesture = KEY_GESTURE_RIGHT;
        break;
    case GESTURE_UP:
        gesture = KEY_GESTURE_UP;
        break;
    case GESTURE_DOWN:
        gesture = KEY_GESTURE_DOWN;
        break;
    case GESTURE_DOUBLECLICK:
        gesture = KEY_GESTURE_U;
        break;
    case GESTURE_O:
        gesture = KEY_GESTURE_O;
        break;
    case GESTURE_W:
        gesture = KEY_GESTURE_W;
        break;
    case GESTURE_M:
        gesture = KEY_GESTURE_M;
        break;
    case GESTURE_E:
        gesture = KEY_GESTURE_E;
        break;
    case GESTURE_L:
        gesture = KEY_GESTURE_L;
        break;
    case GESTURE_S:
        gesture = KEY_GESTURE_S;
        break;
    case GESTURE_V:
        gesture = KEY_GESTURE_V;
        break;
    case GESTURE_Z:
        gesture = KEY_GESTURE_Z;
        break;
    case  GESTURE_C:
        gesture = KEY_GESTURE_C;
        break;
    default:
        gesture = -1;
        break;
    }
    /* report event key */
    if (gesture != -1) {
        FTS_DEBUG("Gesture Code=%d", gesture);
#ifdef FOCALTECH_SENSOR_EN
        if (!(fts_data->wakeable && fts_data->should_enable_gesture)) {
            FTS_INFO("Gesture got but wakeable not set. Skip this gesture.");
            return;
        }
        if (fts_data->pdata->report_gesture_key) {
            pr_info("[FTS_TS] Gesture detected, reporting KEY_WAKEUP\n");
            input_report_key(fts_data->sensor_pdata->input_sensor_dev, KEY_WAKEUP, 1);
            input_sync(fts_data->sensor_pdata->input_sensor_dev);
            input_report_key(fts_data->sensor_pdata->input_sensor_dev, KEY_WAKEUP, 0);
            input_sync(fts_data->sensor_pdata->input_sensor_dev);
        } else {
            input_report_abs(fts_data->sensor_pdata->input_sensor_dev,
                            ABS_DISTANCE, ++report_cnt);
            FTS_INFO("input report: %d", report_cnt);
            if (report_cnt >= REPORT_MAX_COUNT)
                report_cnt = 0;
            input_sync(fts_data->sensor_pdata->input_sensor_dev);
        }
#ifdef CONFIG_HAS_WAKELOCK
        wake_lock_timeout(&gesture_wakelock, msecs_to_jiffies(5000));
#else
        __pm_wakeup_event(&gesture_wakelock, 5000);
#endif
#else
        input_report_key(input_dev, gesture, 1);
        input_sync(input_dev);
        input_report_key(input_dev, gesture, 0);
        input_sync(input_dev);
#endif
    }
}

#ifdef FOCALTECH_SENSOR_EN
static int fts_sensor_set_enable(struct sensors_classdev *sensors_cdev,
    unsigned int enable)
{
    FTS_INFO("Gesture set enable %d!", enable);
    mutex_lock(&fts_data->state_mutex);
    if (enable == 1) {
        fts_data->should_enable_gesture = true;
        touch_set_state(FTS_PALM_SENSOR, TOUCH_PANEL_IDX_PRIMARY);
    } else if (enable == 0) {
        fts_data->should_enable_gesture = false;
        touch_set_state(0, TOUCH_PANEL_IDX_PRIMARY);
    } else {
        FTS_INFO("unknown enable symbol\n");
    }

    mutex_unlock(&fts_data->state_mutex);
    return 0;
}

static int fts_sensor_init(struct fts_ts_data *data)
{
    struct focaltech_sensor_platform_data *sensor_pdata;
    struct input_dev *sensor_input_dev;
    int err;

    sensor_input_dev = input_allocate_device();
    if (!sensor_input_dev) {
        FTS_ERROR("Failed to allocate device");
        goto exit;
    }

    sensor_pdata = devm_kzalloc(&sensor_input_dev->dev,
                                sizeof(struct focaltech_sensor_platform_data),
                                GFP_KERNEL);
    if (!sensor_pdata) {
        FTS_ERROR("Failed to allocate memory");
        goto free_sensor_pdata;
    }
    data->sensor_pdata = sensor_pdata;

    if (data->pdata->report_gesture_key) {
        __set_bit(EV_KEY, sensor_input_dev->evbit);
        __set_bit(KEY_WAKEUP, sensor_input_dev->keybit);
    }
    else {
        __set_bit(EV_ABS, sensor_input_dev->evbit);
        input_set_abs_params(sensor_input_dev, ABS_DISTANCE,
                                0, REPORT_MAX_COUNT, 0, 0);
    }
    __set_bit(EV_SYN, sensor_input_dev->evbit);

    sensor_input_dev->name = "double-tap";
    data->sensor_pdata->input_sensor_dev = sensor_input_dev;

    err = input_register_device(sensor_input_dev);
    if (err) {
        FTS_ERROR("Unable to register device, err=%d", err);
        goto free_sensor_input_dev;
    }

    sensor_pdata->ps_cdev = sensors_touch_cdev;
    sensor_pdata->ps_cdev.sensors_enable = fts_sensor_set_enable;
    sensor_pdata->data = data;

    err = sensors_classdev_register(&sensor_input_dev->dev,
                                    &sensor_pdata->ps_cdev);
    if (err)
        goto unregister_sensor_input_device;

    return 0;

unregister_sensor_input_device:
    input_unregister_device(data->sensor_pdata->input_sensor_dev);
free_sensor_input_dev:
    input_free_device(data->sensor_pdata->input_sensor_dev);
free_sensor_pdata:
    devm_kfree(&sensor_input_dev->dev, sensor_pdata);
    data->sensor_pdata = NULL;
exit:
    return 1;
}

int fts_sensor_remove(struct fts_ts_data *data)
{
    sensors_classdev_unregister(&data->sensor_pdata->ps_cdev);
    input_unregister_device(data->sensor_pdata->input_sensor_dev);
    devm_kfree(&data->sensor_pdata->input_sensor_dev->dev,
               data->sensor_pdata);
    data->sensor_pdata = NULL;
    data->wakeable = false;
    data->should_enable_gesture = false;
    return 0;
}
#endif

/* Doubletap2wake */
void doubletap2wake_reset(void)
{
    dt_state = DT2W_STATE_IDLE;
    tap1_down_time = 0;
    tap1_up_time = 0;
    tap1_x = 0;
    tap1_y = 0;
}

static unsigned int calc_feather(int coord, int prev_coord)
{
    int calc_coord = coord - prev_coord;
    return (calc_coord < 0) ? -calc_coord : calc_coord;
}

bool detect_doubletap2wake(int x, int y, int flag)
{
    unsigned long long now = ktime_to_ms(ktime_get());

    /* Check for timeout when waiting for tap 2 */
    if (dt_state == DT2W_STATE_TAP1_UP) {
        if ((now - tap1_down_time > DT2W_TOTAL_MAX_TIME) ||
            (now - tap1_up_time > DT2W_GAP_MAX)) {
            doubletap2wake_reset();
        }
    }

    if (EVENT_DOWN(flag)) {
        if (dt_state == DT2W_STATE_IDLE) {
            /* First tap down */
            tap1_down_time = now;
            tap1_x = x;
            tap1_y = y;
            dt_state = DT2W_STATE_TAP1_DOWN;
            return false;
        } else if (dt_state == DT2W_STATE_TAP1_DOWN) {
            /* Finger is still down during first tap */
            if (calc_feather(x, tap1_x) > DT2W_FEATHER ||
                calc_feather(y, tap1_y) > DT2W_FEATHER) {
                /* Movement exceeded threshold: swipe or drag, reject */
                dt_state = DT2W_STATE_REJECT;
                return false;
            }
            if ((now - tap1_down_time) > DT2W_TAP_MAX_DURATION) {
                /* Held too long: long press or hold, reject */
                dt_state = DT2W_STATE_REJECT;
                return false;
            }
            /* Still in valid first tap contact */
            return false;
        } else if (dt_state == DT2W_STATE_TAP1_UP) {
            /* Second tap down! */
            unsigned long long gap = now - tap1_up_time;
            unsigned long long total = now - tap1_down_time;

            if (gap < DT2W_GAP_MIN) {
                /* Bounce / contact chatter, ignore */
                return false;
            }

            if (gap <= DT2W_GAP_MAX && total <= DT2W_TOTAL_MAX_TIME &&
                calc_feather(x, tap1_x) <= DT2W_FEATHER &&
                calc_feather(y, tap1_y) <= DT2W_FEATHER) {
                pr_info("[FTS_TS] DT2W recognized! tap1_dur=%llums, gap=%llums, total=%llums, dist=(%d,%d)\n",
                        tap1_up_time - tap1_down_time, gap, total,
                        calc_feather(x, tap1_x), calc_feather(y, tap1_y));
                doubletap2wake_reset();
                return true;
            }

            /* Outside window or distance; start over with this as new tap 1 */
            tap1_down_time = now;
            tap1_x = x;
            tap1_y = y;
            dt_state = DT2W_STATE_TAP1_DOWN;
            return false;
        } else if (dt_state == DT2W_STATE_REJECT) {
            /* Still in rejected touch; ignore until finger lifts */
            return false;
        }
    } else if (EVENT_UP(flag)) {
        if (dt_state == DT2W_STATE_TAP1_DOWN) {
            unsigned long long duration = now - tap1_down_time;
            if (duration <= DT2W_TAP_MAX_DURATION) {
                /* Valid first tap completed! */
                tap1_up_time = now;
                dt_state = DT2W_STATE_TAP1_UP;
            } else {
                doubletap2wake_reset();
            }
            return false;
        } else if (dt_state == DT2W_STATE_REJECT) {
            /* Finger lifted after rejected touch; reset to IDLE */
            doubletap2wake_reset();
            return false;
        }
    }

    return false;
}

/*****************************************************************************
* Name: fts_gesture_readdata
* Brief: Read information about gesture: enable flag/gesture points..., if ges-
*        ture enable, save gesture points' information, and report to OS.
*        It will be called this function every intrrupt when FTS_GESTURE_EN = 1
*
*        gesture data length: 1(enable) + 1(reserve) + 2(header) + 6 * 4
* Input: ts_data - global struct data
*        data    - gesture data buffer if non-flash, else NULL
* Output:
* Return: 0 - read gesture data successfully, the report data is gesture data
*         1 - tp not in suspend/gesture not enable in TP FW
*         -Exx - error
*****************************************************************************/
int fts_gesture_readdata(struct fts_ts_data *ts_data, u8 *data)
{
    int ret = 0;
    int i = 0;
    int index = 0;
    u8 buf[FTS_GESTURE_DATA_LEN] = { 0 };
    u8 gesture_en = 0;
    struct input_dev *input_dev = ts_data->input_dev;
    struct fts_gesture_st *gesture = &fts_gesture_data;

    if (!ts_data->suspended || (DISABLE == gesture->mode)) {
        return 1;
    }

    ret = fts_read_reg(FTS_REG_GESTURE_EN, &gesture_en);
    if ((ret < 0) || (gesture_en != ENABLE)) {
        return 1;
    }

    buf[2] = FTS_REG_GESTURE_OUTPUT_ADDRESS;
    ret = fts_read(&buf[2], 1, &buf[2], FTS_GESTURE_DATA_LEN - 2);
    if (ret < 0) {
        FTS_ERROR("read gesture header data fail: %d\n", ret);
        return ret;
    }

    gesture->gesture_id = buf[2];
    gesture->point_num = buf[3];
    if (gesture->gesture_id != 0) {
        pr_info("[FTS_TS] gesture reg read: gesture_id=0x%02x, point_num=%d\n",
                gesture->gesture_id, gesture->point_num);
    }

    /* save point data, max: 6 */
    for (i = 0; i < FTS_GESTURE_POINTS_MAX; i++) {
        index = 4 * i + 4;
        gesture->coordinate_x[i] = (u16)(((buf[0 + index] & 0x0F) << 8)
                                         + buf[1 + index]);
        gesture->coordinate_y[i] = (u16)(((buf[2 + index] & 0x0F) << 8)
                                         + buf[3 + index]);
    }

    /* report gesture to OS */
    if (gesture->gesture_id == GESTURE_DOUBLECLICK) {
        pr_info("[FTS_TS] Hardware gesture GESTURE_DOUBLECLICK recognized!\n");
        fts_gesture_report(input_dev, GESTURE_DOUBLECLICK);
        return 0;
    }

    return 1;
}

void fts_gesture_recovery(struct fts_ts_data *ts_data)
{
    if ((ENABLE == fts_gesture_data.mode) && (ENABLE == fts_gesture_data.active)) {
        FTS_DEBUG("gesture recovery...");
        fts_write_reg(0xD1, 0xFF);
        fts_write_reg(0xD2, 0xFF);
        fts_write_reg(0xD5, 0xFF);
        fts_write_reg(0xD6, 0xFF);
        fts_write_reg(0xD7, 0xFF);
        fts_write_reg(0xD8, 0xFF);
        fts_write_reg(FTS_REG_GESTURE_EN, ENABLE);
    }
}

int fts_gesture_suspend(struct fts_ts_data *ts_data)
{
    int ret = 0;
    int i = 0;
    u8 state = 0xFF;

    doubletap2wake_reset();
    FTS_INFO("gesture suspend...");
    /* gesture not enable, return immediately */
    if (fts_gesture_data.mode == DISABLE) {
        FTS_DEBUG("gesture is disabled");
        return -EINVAL;
    }

    for (i = 0; i < 5; i++) {
        fts_write_reg(0xD1, 0xFF);
        fts_write_reg(0xD2, 0xFF);
        fts_write_reg(0xD5, 0xFF);
        fts_write_reg(0xD6, 0xFF);
        fts_write_reg(0xD7, 0xFF);
        fts_write_reg(0xD8, 0xFF);
        fts_write_reg(FTS_REG_GESTURE_EN, ENABLE);
        msleep(1);
        fts_read_reg(FTS_REG_GESTURE_EN, &state);
        if (state == ENABLE)
            break;
    }

    if (i >= 5) {
        FTS_ERROR("Enter into gesture(suspend) fail");
        fts_gesture_data.active = DISABLE;
        return -EIO;
    }

    ret = enable_irq_wake(ts_data->irq);
    if (ret) {
        FTS_DEBUG("enable_irq_wake(irq:%d) fail", ts_data->irq);
    }

    fts_gesture_data.active = ENABLE;
    pr_info("[FTS_TS] Enter into gesture(suspend) successfully!\n");
    return 0;
}

int fts_gesture_resume(struct fts_ts_data *ts_data)
{
    int ret = 0;
    int i = 0;
    u8 state = 0xFF;

    doubletap2wake_reset();
    FTS_INFO("gesture resume...");
    /* gesture not enable, return immediately */
    if (fts_gesture_data.mode == DISABLE) {
        FTS_DEBUG("gesture is disabled");
        return -EINVAL;
    }

    if (fts_gesture_data.active == DISABLE) {
        FTS_DEBUG("gesture active is disable, return immediately");
        return -EINVAL;
    }

    fts_gesture_data.active = DISABLE;
    for (i = 0; i < 5; i++) {
        fts_write_reg(FTS_REG_GESTURE_EN, DISABLE);
        msleep(1);
        fts_read_reg(FTS_REG_GESTURE_EN, &state);
        if (state == DISABLE)
            break;
    }

    if (i >= 5) {
        FTS_ERROR("exit gesture(resume) fail");
        return -EIO;
    }

    ret = disable_irq_wake(ts_data->irq);
    if (ret) {
        FTS_DEBUG("disable_irq_wake(irq:%d) fail", ts_data->irq);
    }

    FTS_INFO("resume from gesture successfully");
    return 0;
}

int fts_gesture_init(struct fts_ts_data *ts_data)
{
    struct input_dev *input_dev = ts_data->input_dev;
#ifdef FOCALTECH_SENSOR_EN
    static bool initialized_sensor;
#endif

    FTS_FUNC_ENTER();
    input_set_capability(input_dev, EV_KEY, KEY_POWER);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_U);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_UP);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_DOWN);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_LEFT);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_RIGHT);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_O);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_E);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_M);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_L);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_W);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_S);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_V);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_Z);
    input_set_capability(input_dev, EV_KEY, KEY_GESTURE_C);

    __set_bit(KEY_GESTURE_RIGHT, input_dev->keybit);
    __set_bit(KEY_GESTURE_LEFT, input_dev->keybit);
    __set_bit(KEY_GESTURE_UP, input_dev->keybit);
    __set_bit(KEY_GESTURE_DOWN, input_dev->keybit);
    __set_bit(KEY_GESTURE_U, input_dev->keybit);
    __set_bit(KEY_GESTURE_O, input_dev->keybit);
    __set_bit(KEY_GESTURE_E, input_dev->keybit);
    __set_bit(KEY_GESTURE_M, input_dev->keybit);
    __set_bit(KEY_GESTURE_W, input_dev->keybit);
    __set_bit(KEY_GESTURE_L, input_dev->keybit);
    __set_bit(KEY_GESTURE_S, input_dev->keybit);
    __set_bit(KEY_GESTURE_V, input_dev->keybit);
    __set_bit(KEY_GESTURE_C, input_dev->keybit);
    __set_bit(KEY_GESTURE_Z, input_dev->keybit);

#ifdef FOCALTECH_SENSOR_EN
    if (!initialized_sensor) {
#ifdef CONFIG_HAS_WAKELOCK
        wake_lock_init(&gesture_wakelock, WAKE_LOCK_SUSPEND, "poll-wake-lock");
#else
        wakeup_source_init(&gesture_wakelock, "ets_wake_lock");
#endif
        if (!fts_sensor_init(ts_data))
            initialized_sensor = true;
    }
#endif

    fts_create_gesture_sysfs(ts_data->dev);

    memset(&fts_gesture_data, 0, sizeof(struct fts_gesture_st));
    fts_gesture_data.mode = ENABLE;
    fts_gesture_data.active = DISABLE;

    FTS_FUNC_EXIT();
    return 0;
}

int fts_gesture_exit(struct fts_ts_data *ts_data)
{
    FTS_FUNC_ENTER();
    sysfs_remove_group(&ts_data->dev->kobj, &fts_gesture_group);
    FTS_FUNC_EXIT();
    return 0;
}
#endif
