/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT microcrystal_rv_3029_c2

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "rtc_utils.h"

LOG_MODULE_REGISTER(rv3029, CONFIG_RTC_LOG_LEVEL);

#define RV3029_REG_CONTROL_1       0x00
#define RV3029_REG_CONTROL_INT     0x01
#define RV3029_REG_CONTROL_FLAGS   0x02
#define RV3029_REG_STATUS          0x03
#define RV3029_REG_RESET           0x04
#define RV3029_REG_SECONDS         0x08
#define RV3029_REG_MINUTES         0x09
#define RV3029_REG_HOURS           0x0A
#define RV3029_REG_DATE            0x0B
#define RV3029_REG_WEEKDAY         0x0C
#define RV3029_REG_MONTH           0x0D
#define RV3029_REG_YEAR            0x0E

#define RV3029_SECONDS_MASK        GENMASK(6, 0)
#define RV3029_MINUTES_MASK        GENMASK(6, 0)
#define RV3029_HOURS_MASK          GENMASK(5, 0)
#define RV3029_DATE_MASK           GENMASK(5, 0)
#define RV3029_WEEKDAY_MASK        GENMASK(2, 0)
#define RV3029_MONTH_MASK          GENMASK(4, 0)
#define RV3029_YEAR_MASK           GENMASK(7, 0)

#define RV3029_YEAR_OFFSET         (2000 - 1900)
#define RV3029_MONTH_OFFSET        1

#define RV3029_RTC_TIME_MASK                                                                    \
	(RTC_ALARM_TIME_MASK_SECOND | RTC_ALARM_TIME_MASK_MINUTE | RTC_ALARM_TIME_MASK_HOUR |  \
	 RTC_ALARM_TIME_MASK_MONTH | RTC_ALARM_TIME_MASK_MONTHDAY | RTC_ALARM_TIME_MASK_YEAR | \
	 RTC_ALARM_TIME_MASK_WEEKDAY)

struct rv3029_config {
	struct i2c_dt_spec i2c;
};

struct rv3029_data {
	struct k_sem lock;
};

static int rv3029_read_regs(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len)
{
	const struct rv3029_config *cfg = dev->config;

	return i2c_write_read_dt(&cfg->i2c, &reg, sizeof(reg), buf, len);
}

static int rv3029_write_regs(const struct device *dev, uint8_t reg, const uint8_t *buf, size_t len)
{
	const struct rv3029_config *cfg = dev->config;
	uint8_t block[1 + len];

	block[0] = reg;
	memcpy(&block[1], buf, len);

	return i2c_write_dt(&cfg->i2c, block, sizeof(block));
}

static int rv3029_set_time(const struct device *dev, const struct rtc_time *timeptr)
{
	uint8_t regs[7];
	struct rv3029_data *data = dev->data;

	if ((timeptr == NULL) || !rtc_utils_validate_rtc_time(timeptr, RV3029_RTC_TIME_MASK) ||
	    (timeptr->tm_year < RV3029_YEAR_OFFSET)) {
		return -EINVAL;
	}

	regs[0] = bin2bcd(timeptr->tm_sec) & RV3029_SECONDS_MASK;
	regs[1] = bin2bcd(timeptr->tm_min) & RV3029_MINUTES_MASK;
	regs[2] = bin2bcd(timeptr->tm_hour) & RV3029_HOURS_MASK;
	regs[3] = bin2bcd(timeptr->tm_mday) & RV3029_DATE_MASK;
	regs[4] = bin2bcd(timeptr->tm_wday) & RV3029_WEEKDAY_MASK;
	regs[5] = bin2bcd(timeptr->tm_mon + RV3029_MONTH_OFFSET) & RV3029_MONTH_MASK;
	regs[6] = bin2bcd(timeptr->tm_year - RV3029_YEAR_OFFSET) & RV3029_YEAR_MASK;

	(void)k_sem_take(&data->lock, K_FOREVER);
	int ret = rv3029_write_regs(dev, RV3029_REG_SECONDS, regs, sizeof(regs));
	k_sem_give(&data->lock);

	return ret;
}

static int rv3029_get_time(const struct device *dev, struct rtc_time *timeptr)
{
	uint8_t regs[7];
	struct rv3029_data *data = dev->data;
	int ret;

	if (timeptr == NULL) {
		return -EINVAL;
	}

	(void)k_sem_take(&data->lock, K_FOREVER);
	ret = rv3029_read_regs(dev, RV3029_REG_SECONDS, regs, sizeof(regs));
	k_sem_give(&data->lock);
	if (ret < 0) {
		return ret;
	}

	timeptr->tm_sec = bcd2bin(regs[0] & RV3029_SECONDS_MASK);
	timeptr->tm_min = bcd2bin(regs[1] & RV3029_MINUTES_MASK);
	timeptr->tm_hour = bcd2bin(regs[2] & RV3029_HOURS_MASK);
	timeptr->tm_mday = bcd2bin(regs[3] & RV3029_DATE_MASK);
	timeptr->tm_wday = bcd2bin(regs[4] & RV3029_WEEKDAY_MASK);
	timeptr->tm_mon = bcd2bin(regs[5] & RV3029_MONTH_MASK) - RV3029_MONTH_OFFSET;
	timeptr->tm_year = bcd2bin(regs[6] & RV3029_YEAR_MASK) + RV3029_YEAR_OFFSET;
	timeptr->tm_nsec = 0;
	timeptr->tm_isdst = -1;
	timeptr->tm_yday = -1;

	if (!rtc_utils_validate_rtc_time(timeptr, RV3029_RTC_TIME_MASK)) {
		return -ENODATA;
	}

	return 0;
}

static int rv3029_init(const struct device *dev)
{
	const struct rv3029_config *cfg = dev->config;
	struct rv3029_data *data = dev->data;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	k_sem_init(&data->lock, 1, 1);

	return 0;
}

static DEVICE_API(rtc, rv3029_driver_api) = {
	.set_time = rv3029_set_time,
	.get_time = rv3029_get_time,
};

#define RV3029_DEFINE(inst)                                                                       \
	static struct rv3029_data rv3029_data_##inst;                                               \
	static const struct rv3029_config rv3029_config_##inst = {                                  \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                    \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, rv3029_init, NULL, &rv3029_data_##inst,                        \
			      &rv3029_config_##inst, POST_KERNEL, CONFIG_RTC_INIT_PRIORITY,             \
			      &rv3029_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RV3029_DEFINE)