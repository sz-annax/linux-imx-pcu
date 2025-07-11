// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MAXI MAX11801 - A Resistive touch screen controller with
 * i2c interface
 *
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc.
 * Author: Zhang Jiejing <jiejing.zhang@freescale.com>
 *
 * Based on mcs5000_ts.c
 */

/*
 * This driver aims to support the series of MAXI touch chips max11801
 * through max11803. The main difference between these 4 chips can be
 * found in the table below:
 * -----------------------------------------------------
 * | CHIP     |  AUTO MODE SUPPORT(FIFO) | INTERFACE    |
 * |----------------------------------------------------|
 * | max11800 |  YES                     |   SPI        |
 * | max11801 |  YES                     |   I2C        |
 * | max11802 |  NO                      |   SPI        |
 * | max11803 |  NO                      |   I2C        |
 * ------------------------------------------------------
 *
 * Currently, this driver only supports max11801.
 *
 * Data Sheet:
 * http://www.maxim-ic.com/datasheet/index.mvp/id/5943
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>

/* Register Address define */
#define GENERNAL_STATUS_REG		0x00
#define GENERNAL_CONF_REG		0x01
#define MESURE_RES_CONF_REG		0x02
#define MESURE_AVER_CONF_REG		0x03
#define ADC_SAMPLE_TIME_CONF_REG	0x04
#define PANEL_SETUPTIME_CONF_REG	0x05
#define DELAY_CONVERSION_CONF_REG	0x06
#define TOUCH_DETECT_PULLUP_CONF_REG	0x07
#define AUTO_MODE_TIME_CONF_REG		0x08 /* only for max11800/max11801 */
#define APERTURE_CONF_REG		0x09 /* only for max11800/max11801 */
#define AUX_MESURE_CONF_REG		0x0a
#define OP_MODE_CONF_REG		0x0b

#define Panel_Setup_X	(0x69 << 1)
#define Panel_Setup_Y	(0x6b << 1)

#define XY_combined_measurement	(0x70 << 1)
#define X_measurement	(0x78 << 1)
#define Y_measurement	(0x7a << 1)
#define AUX_measurement		(0x76 << 1)

/* FIFO is found only in max11800 and max11801 */
#define FIFO_RD_CMD			(0x50 << 1)
#define MAX11801_FIFO_INT		(1 << 2)
#define MAX11801_FIFO_OVERFLOW		(1 << 3)
#define MAX11801_EDGE_INT		(1 << 1)

#define FIFO_RD_X_MSB			(0x52 << 1)
#define FIFO_RD_X_LSB			(0x53 << 1)
#define FIFO_RD_Y_MSB			(0x54 << 1)
#define FIFO_RD_Y_LSB			(0x55 << 1)
#define FIFO_RD_AUX_MSB			(0x5a << 1)
#define FIFO_RD_AUX_LSB			(0x5b << 1)

#define XY_BUFSIZE			4
#define XY_BUF_OFFSET			4
#define AUX_BUFSIZE			2

#define MAX11801_MAX_X			0xfff
#define MAX11801_MAX_Y			0xfff

#define MEASURE_TAG_OFFSET		2
#define MEASURE_TAG_MASK		(3 << MEASURE_TAG_OFFSET)
#define EVENT_TAG_OFFSET		0
#define EVENT_TAG_MASK			(3 << EVENT_TAG_OFFSET)
#define MEASURE_X_TAG			(0 << MEASURE_TAG_OFFSET)
#define MEASURE_Y_TAG			(1 << MEASURE_TAG_OFFSET)

/* These are the state of touch event state machine */
enum {
	EVENT_INIT,
	EVENT_MIDDLE,
	EVENT_RELEASE,
	EVENT_FIFO_END
};

struct max11801_data {
	struct i2c_client		*client;
	struct input_dev		*input_dev;
};

static struct i2c_client *max11801_client;
static unsigned int max11801_workmode;
static u8 aux_buf[AUX_BUFSIZE];
u32 max11801_read_adc(void);

static int max11801_dcm_write_command(struct i2c_client *client, int command)
{
	return i2c_smbus_write_byte(client, command);
}

static u32 max11801_dcm_sample_aux(struct i2c_client *client)
{
	int ret;
	int aux = 0;
	u32 sample_data;

	/* AUX_measurement */
	max11801_dcm_write_command(client, AUX_measurement);
	mdelay(5);
	ret = i2c_smbus_read_i2c_block_data(client, FIFO_RD_AUX_MSB,
						1, &aux_buf[0]);
	if (ret < 1) {
		dev_err(&client->dev, "FIFO_RD_AUX_MSB read fails\n");
		return ret;
	}
	mdelay(5);
	ret = i2c_smbus_read_i2c_block_data(client, FIFO_RD_AUX_LSB,
						1, &aux_buf[1]);
	if (ret < 1) {
		dev_err(&client->dev, "FIFO_RD_AUX_LSB read fails\n");
		return ret;
	}

	aux = (aux_buf[0] << 4) + (aux_buf[1] >> 4);
	/*
	 * voltage = (9170*aux)/7371;
	 * voltage is (26.2*3150*aux)/(16.2*0xFFF)
	 * V(aux)=3150*sample/0xFFF,V(battery)=212*V(aux)/81
	 * sample_data = (14840*aux)/7371-1541;
	 */
	sample_data = (14840 * aux) / 7371;

	return sample_data;
}

u32 max11801_read_adc(void)
{
	u32 adc_data;

	if (!max11801_client) {
		pr_err("FAIL  max11801_client not initialize\n");
		return -1;
	}
	adc_data = max11801_dcm_sample_aux(max11801_client);

	return adc_data;
}
EXPORT_SYMBOL_GPL(max11801_read_adc);

static u8 read_register(struct i2c_client *client, int addr)
{
	/* XXX: The chip ignores LSB of register address */
	return i2c_smbus_read_byte_data(client, addr << 1);
}

static int max11801_write_reg(struct i2c_client *client, int addr, int data)
{
	/* XXX: The chip ignores LSB of register address */
	return i2c_smbus_write_byte_data(client, addr << 1, data);
}

static irqreturn_t max11801_ts_interrupt(int irq, void *dev_id)
{
	struct max11801_data *data = dev_id;
	struct i2c_client *client = data->client;
	int status, i, ret;
	u8 buf[XY_BUFSIZE];
	int x = -1;
	int y = -1;
	u8 command = FIFO_RD_X_MSB;

	status = read_register(data->client, GENERNAL_STATUS_REG);
	if ((!max11801_workmode && (status & (MAX11801_FIFO_INT |
		MAX11801_FIFO_OVERFLOW))) || (max11801_workmode && (status &
		MAX11801_EDGE_INT))) {
		status = read_register(data->client, GENERNAL_STATUS_REG);
		if (!max11801_workmode) {
			/* ACM mode */
			ret = i2c_smbus_read_i2c_block_data(client, FIFO_RD_CMD,
				XY_BUFSIZE, buf);
			/*
			 * We should get 4 bytes buffer that contains X,Y
			 * and event tag
			 */
			if (ret < XY_BUFSIZE)
				goto out;
		} else {
			/* DCM mode */
			/* X = panel setup */
			max11801_dcm_write_command(client, Panel_Setup_X);
			/* X_measurement */
			max11801_dcm_write_command(client, X_measurement);
			for (i = 0; i < 2; i++) {
				ret = i2c_smbus_read_i2c_block_data(client,
					command, 1, &buf[i]);
				if (ret < 1)
					goto out;

				command = FIFO_RD_X_LSB;
			}

			/* Y = panel setup */
			max11801_dcm_write_command(client, Panel_Setup_Y);
			/* Y_measurement */
			max11801_dcm_write_command(client, Y_measurement);
			command = FIFO_RD_Y_MSB;
			for (i = 2; i < XY_BUFSIZE; i++) {
				ret = i2c_smbus_read_i2c_block_data(client,
					command, 1, &buf[i]);
				if (ret < 1)
					goto out;

				command = FIFO_RD_Y_LSB;
			}
		}

		for (i = 0; i < XY_BUFSIZE; i += XY_BUFSIZE / 2) {
			if ((buf[i + 1] & MEASURE_TAG_MASK) == MEASURE_X_TAG)
				x = (buf[i] << XY_BUF_OFFSET) +
				    (buf[i + 1] >> XY_BUF_OFFSET);
			else if ((buf[i + 1] & MEASURE_TAG_MASK) == MEASURE_Y_TAG)
				y = (buf[i] << XY_BUF_OFFSET) +
				    (buf[i + 1] >> XY_BUF_OFFSET);
		}

		if ((buf[1] & EVENT_TAG_MASK) != (buf[3] & EVENT_TAG_MASK))
			goto out;

		switch (buf[1] & EVENT_TAG_MASK) {
		case EVENT_INIT:
		case EVENT_MIDDLE:
			input_report_abs(data->input_dev, ABS_X, x);
			y = MAX11801_MAX_Y - y;	/* Calibration */
			input_report_abs(data->input_dev, ABS_Y, y);
			input_event(data->input_dev, EV_KEY, BTN_TOUCH, 1);
			input_sync(data->input_dev);
			break;

		case EVENT_RELEASE:
			input_event(data->input_dev, EV_KEY, BTN_TOUCH, 0);
			input_sync(data->input_dev);
			break;

		case EVENT_FIFO_END:
			break;
		}
	}
out:
	return IRQ_HANDLED;
}

static void max11801_ts_phy_init(struct max11801_data *data)
{
	struct i2c_client *client = data->client;

	max11801_client = client;
	/* Average X,Y, take 16 samples average eight media sample */
	max11801_write_reg(client, MESURE_AVER_CONF_REG, 0xff);
	/* X,Y panel setup time set to 20us */
	max11801_write_reg(client, PANEL_SETUPTIME_CONF_REG, 0x11);
	/* Rough pullup time (2uS), Fine pullup time (10us)  */
	max11801_write_reg(client, TOUCH_DETECT_PULLUP_CONF_REG, 0x10);
	/* Auto mode init period = 5ms , scan period = 5ms*/
	max11801_write_reg(client, AUTO_MODE_TIME_CONF_REG, 0xaa);
	/* Aperture X,Y set to +- 4LSB */
	max11801_write_reg(client, APERTURE_CONF_REG, 0x33);
	/* Enable Power, enable Automode, enable Aperture, enable Average X,Y */
	if (!max11801_workmode)
		max11801_write_reg(client, OP_MODE_CONF_REG, 0x36);
	else {
		max11801_write_reg(client, OP_MODE_CONF_REG, 0x16);
		/*
		 * Delay initial=1ms, Sampling time 2us
		 * Averaging sample depth 2
		 * samples, Resolution 12bit
		 */
		max11801_write_reg(client, AUX_MESURE_CONF_REG, 0x76);
		/*
		 * Use edge interrupt with
		 * direct conversion mode
		 */
		max11801_write_reg(client, GENERNAL_CONF_REG, 0xf3);
	}
}

static int max11801_ts_probe(struct i2c_client *client)
{
	struct max11801_data *data;
	struct input_dev *input_dev;
	int error;
	struct device_node *of_node = client->dev.of_node;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	input_dev = devm_input_allocate_device(&client->dev);
	if (!data || !input_dev) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	data->client = client;
	data->input_dev = input_dev;

	input_dev->name = "max11801_ts";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	input_set_abs_params(input_dev, ABS_X, 0, MAX11801_MAX_X, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MAX11801_MAX_Y, 0, 0);

	if (of_property_read_u32(of_node, "work-mode", &max11801_workmode))
		max11801_workmode = *(int *)(client->dev).platform_data;

	max11801_ts_phy_init(data);

	error = devm_request_threaded_irq(&client->dev, client->irq, NULL,
					  max11801_ts_interrupt,
					  IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					  "max11801_ts", data);
	if (error) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		return error;
	}

	error = input_register_device(data->input_dev);
	if (error)
		return error;

	return 0;
}

static const struct i2c_device_id max11801_ts_id[] = {
	{ "max11801" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max11801_ts_id);

static const struct of_device_id max11801_ts_dt_ids[] = {
	{ .compatible = "maxim,max11801" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, max11801_ts_dt_ids);

static struct i2c_driver max11801_ts_driver = {
	.driver = {
		.name	= "max11801_ts",
		.of_match_table = max11801_ts_dt_ids,
	},
	.id_table	= max11801_ts_id,
	.probe		= max11801_ts_probe,
};

module_i2c_driver(max11801_ts_driver);

MODULE_AUTHOR("Zhang Jiejing <jiejing.zhang@freescale.com>");
MODULE_DESCRIPTION("Touchscreen driver for MAXI MAX11801 controller");
MODULE_LICENSE("GPL");
