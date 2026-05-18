/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT st_stc311x

#include "stc311x.h"

#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(STC311X, CONFIG_FUEL_GAUGE_LOG_LEVEL);

#if DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 0
#warning "STC311X driver enabled without any devices"
#endif

static int stc311x_read_reg(const struct device *dev, uint8_t reg, uint8_t *value)
{
	const struct stc311x_config *cfg = dev->config;

	return i2c_burst_read_dt(&cfg->i2c, reg, value, sizeof(*value));
}

static int stc311x_write_reg(const struct device *dev, uint8_t reg, uint8_t value)
{
	const struct stc311x_config *cfg = dev->config;
	uint8_t buffer[] = { reg, value };

	return i2c_write_dt(&cfg->i2c, buffer, sizeof(buffer));
}

static int stc311x_read_u16(const struct device *dev, uint8_t reg, uint16_t *value)
{
	const struct stc311x_config *cfg = dev->config;
	uint8_t buffer[2];
	int ret;

	ret = i2c_burst_read_dt(&cfg->i2c, reg, buffer, sizeof(buffer));
	if (ret < 0) {
		return ret;
	}

	*value = sys_get_le16(buffer);
	return 0;
}

static int stc311x_write_u16(const struct device *dev, uint8_t reg, uint16_t value)
{
	const struct stc311x_config *cfg = dev->config;
	uint8_t buffer[3];

	buffer[0] = reg;
	sys_put_le16(value, &buffer[1]);

	return i2c_write_dt(&cfg->i2c, buffer, sizeof(buffer));
}

static uint16_t stc311x_calc_cc_cnf(const struct stc311x_config *cfg)
{
	uint64_t numerator = (uint64_t)cfg->rsense_milliohm * cfg->design_capacity * 1000U;
	uint16_t value = (uint16_t)DIV_ROUND_CLOSEST(numerator, 49556U);

	return MAX(value, 1U);
}

static uint16_t stc311x_calc_vm_cnf(const struct stc311x_config *cfg)
{
	uint64_t numerator =
		(uint64_t)cfg->battery_internal_resistance_milliohm * cfg->design_capacity * 100U;
	uint16_t value = (uint16_t)DIV_ROUND_CLOSEST(numerator, 97778U);

	return MAX(value, 1U);
}

static int stc311x_current_to_ua(const struct stc311x_config *cfg, int16_t raw_value,
					 uint16_t lsb_uv_x100)
{
	int64_t scaled = (int64_t)raw_value * lsb_uv_x100 * 10;

	if (scaled >= 0) {
		scaled += cfg->rsense_milliohm / 2;
	} else {
		scaled -= cfg->rsense_milliohm / 2;
	}

	return (int)(scaled / cfg->rsense_milliohm);
}

static int stc311x_configure(const struct device *dev)
{
	int ret;
	uint8_t mode;
	uint8_t ctrl;

	ret = stc311x_write_u16(dev, STC311X_REG_CC_CNF, stc311x_calc_cc_cnf(dev->config));
	if (ret < 0) {
		return ret;
	}

	ret = stc311x_write_u16(dev, STC311X_REG_VM_CNF, stc311x_calc_vm_cnf(dev->config));
	if (ret < 0) {
		return ret;
	}

	mode = STC311X_MODE_GG_RUN | STC311X_MODE_ALM_ENA | STC311X_MODE_BATD_PU;
	ret = stc311x_write_reg(dev, STC311X_REG_MODE, mode);
	if (ret < 0) {
		return ret;
	}

	ret = stc311x_read_reg(dev, STC311X_REG_CTRL, &ctrl);
	if (ret < 0) {
		return ret;
	}

	if ((ctrl & (STC311X_CTRL_PORDET | STC311X_CTRL_BATFAIL | STC311X_CTRL_ALM_SOC |
		     STC311X_CTRL_ALM_VOLT | STC311X_CTRL_UVLOD)) != 0U) {
		ret = stc311x_write_reg(dev, STC311X_REG_CTRL, STC311X_CTRL_IO0DATA);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int stc311x_get_prop(const struct device *dev, fuel_gauge_prop_t prop,
				    union fuel_gauge_prop_val *val)
{
	const struct stc311x_config *cfg = dev->config;
	uint8_t mode;
	uint8_t ctrl;
	uint16_t raw_u16;
	int16_t raw_s16;
	int ret;

	switch (prop) {
	case FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE:
	case FUEL_GAUGE_ABSOLUTE_STATE_OF_CHARGE:
		ret = stc311x_read_u16(dev, STC311X_REG_SOC, &raw_u16);
		if (ret < 0) {
			return ret;
		}

		raw_u16 = CLAMP(DIV_ROUND_CLOSEST(raw_u16, 512U), 0U, 100U);
		if (prop == FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE) {
			val->relative_state_of_charge = (uint8_t)raw_u16;
		} else {
			val->absolute_state_of_charge = (uint8_t)raw_u16;
		}
		return 0;

	case FUEL_GAUGE_VOLTAGE:
		ret = stc311x_read_u16(dev, STC311X_REG_VOLTAGE, &raw_u16);
		if (ret < 0) {
			return ret;
		}

		val->voltage = (int)raw_u16 * 2200;
		return 0;

	case FUEL_GAUGE_TEMPERATURE: {
		uint8_t raw_temp;
		int8_t temperature_c;

		ret = stc311x_read_reg(dev, STC311X_REG_TEMPERATURE, &raw_temp);
		if (ret < 0) {
			return ret;
		}

		temperature_c = (int8_t)raw_temp;
		val->temperature = (uint16_t)(2731 + (temperature_c * 10));
		return 0;
	}

	case FUEL_GAUGE_CURRENT:
		ret = stc311x_read_u16(dev, STC311X_REG_CURRENT, &raw_u16);
		if (ret < 0) {
			return ret;
		}

		raw_s16 = (int16_t)raw_u16;
		val->current = stc311x_current_to_ua(cfg, raw_s16, 588);
		return 0;

	case FUEL_GAUGE_AVG_CURRENT:
		ret = stc311x_read_reg(dev, STC311X_REG_MODE, &mode);
		if (ret < 0) {
			return ret;
		}

		if ((mode & STC311X_MODE_VM_MODE) != 0U) {
			return -ENOTSUP;
		}

		ret = stc311x_read_u16(dev, STC311X_REG_AVG_CURRENT, &raw_u16);
		if (ret < 0) {
			return ret;
		}

		raw_s16 = (int16_t)raw_u16;
		val->avg_current = stc311x_current_to_ua(cfg, raw_s16, 147);
		return 0;

	case FUEL_GAUGE_STATUS:
	case FUEL_GAUGE_FLAGS:
		ret = stc311x_read_reg(dev, STC311X_REG_CTRL, &ctrl);
		if (ret < 0) {
			return ret;
		}

		if (prop == FUEL_GAUGE_STATUS) {
			val->fg_status = ctrl;
		} else {
			val->flags = ctrl;
		}
		return 0;

	case FUEL_GAUGE_DESIGN_CAPACITY:
		val->design_cap = cfg->design_capacity;
		return 0;

	default:
		return -ENOTSUP;
	}
}

static int stc311x_init(const struct device *dev)
{
	const struct stc311x_config *cfg = dev->config;
	uint8_t part_id;
	int ret;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	ret = stc311x_read_reg(dev, STC311X_REG_ID, &part_id);
	if (ret < 0) {
		LOG_ERR("Failed to read device ID (%d)", ret);
		return ret;
	}

	if (part_id != STC3115_ID && part_id != STC3117_ID) {
		LOG_ERR("Unexpected device ID 0x%02x", part_id);
		return -ENODEV;
	}

	ret = stc311x_configure(dev);
	if (ret < 0) {
		LOG_ERR("Failed to configure device (%d)", ret);
		return ret;
	}

	return 0;
}

static const struct fuel_gauge_driver_api stc311x_driver_api = {
	.get_property = stc311x_get_prop,
};

#define STC311X_INIT(inst)                                                                        \
	static const struct stc311x_config stc311x_config_##inst = {                               \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                    \
		.design_capacity = DT_INST_PROP(inst, design_capacity),                               \
		.rsense_milliohm = DT_INST_PROP(inst, rsense_milliohm),                               \
		.battery_internal_resistance_milliohm =                                              \
			DT_INST_PROP(inst, battery_internal_resistance_milliohm),                      \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, stc311x_init, NULL, NULL, &stc311x_config_##inst,             \
			      POST_KERNEL, CONFIG_FUEL_GAUGE_INIT_PRIORITY, &stc311x_driver_api);

DT_INST_FOREACH_STATUS_OKAY(STC311X_INIT)