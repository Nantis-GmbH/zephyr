/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_FUEL_GAUGE_STC311X_H_
#define ZEPHYR_DRIVERS_FUEL_GAUGE_STC311X_H_

#include <stdint.h>

#include <zephyr/drivers/i2c.h>

#define STC311X_REG_MODE 0x00
#define STC311X_REG_CTRL 0x01
#define STC311X_REG_SOC 0x02
#define STC311X_REG_CURRENT 0x06
#define STC311X_REG_VOLTAGE 0x08
#define STC311X_REG_TEMPERATURE 0x0A
#define STC311X_REG_AVG_CURRENT 0x0B
#define STC311X_REG_CC_CNF 0x0F
#define STC311X_REG_VM_CNF 0x11
#define STC311X_REG_ID 0x18

#define STC3115_ID 0x14
#define STC3117_ID 0x16

#define STC311X_MODE_VM_MODE BIT(0)
#define STC311X_MODE_BATD_PU BIT(1)
#define STC311X_MODE_FORCE_CD BIT(2)
#define STC311X_MODE_ALM_ENA BIT(3)
#define STC311X_MODE_GG_RUN BIT(4)

#define STC311X_CTRL_IO0DATA BIT(0)
#define STC311X_CTRL_GG_RST BIT(1)
#define STC311X_CTRL_GG_VM BIT(2)
#define STC311X_CTRL_BATFAIL BIT(3)
#define STC311X_CTRL_PORDET BIT(4)
#define STC311X_CTRL_ALM_SOC BIT(5)
#define STC311X_CTRL_ALM_VOLT BIT(6)
#define STC311X_CTRL_UVLOD BIT(7)

struct stc311x_config {
	struct i2c_dt_spec i2c;
	uint16_t design_capacity;
	uint16_t rsense_milliohm;
	uint16_t battery_internal_resistance_milliohm;
};

#endif /* ZEPHYR_DRIVERS_FUEL_GAUGE_STC311X_H_ */