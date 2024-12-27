// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Prazen Co. Ltd.
 *
 * Author: TED <mspark@prazen.co>
 */
#include <linux/atomic.h>
#include <linux/delay.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/freezer.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/of_gpio.h>
#include <linux/sensor-dev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>


#define	SYS_CTRL			0x40
#define	MODE_CTRL			0x41
#define ALS_PS_CTRL			0x42
#define	PS_CTRL				0x43
#define	PS_DATA_LSB			0x44
#define	PS_DATA_MSB			0x45
#define	INTERRUPT			0x4A
#define	PS_TH_LSB			0x4B
#define	PS_TH_MSB			0x4C
#define	PS_TL_LSB			0x4D
#define	PS_TL_MSB			0x4E
#define	MANUFACT_ID			0x92

/* MODE_CTRL (0x41) */
#define	PS_DIS				(0 << 6)
#define	PS_EN				(1 << 6)

// measurement itme
#define TIME_ALS_100MS_PS_50MS	(0x05)
#define TIME_ALS_100MS_PS_100MS	(0x06)

/* LED_CTRL (0x42) */
#define ALS_DATA0_GAIN_X1	(0 << 2)
#define ALS_DATA1_GAIN_X1	(0 << 4)
#define LED_CURRENT_100MA	(2 << 0)

/* PS_CTRL (0x43) */
#define PS_GAIN_X1			(0 << 4)

/* INTERRUPT (0x4A) */
#define PS_INT_STATUS		(1 << 7)
#define INT_PS_EN			(1 << 0)
#define INT_PS_ALS_EN		(3 << 0)


static int ps_threshold_low;
static int ps_threshold_high;
static int val_flag;

static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;
	int status = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	if (!enable) {
		status = ~PS_EN;
		sensor->ops->ctrl_data &= status;
	} else {
		status = PS_EN;
		sensor->ops->ctrl_data |= status;
	}

	dev_dbg(&client->dev, "reg=0x%x, reg_ctrl=0x%x, enable=%d\n",
		sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);

	result = sensor_write_reg(client, sensor->ops->ctrl_reg,
				  sensor->ops->ctrl_data);
	if (result)
		dev_err(&client->dev, "%s:fail to active sensor\n", __func__);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	struct device_node *np = client->dev.of_node;
	int ps_val = 0;
	int result = 0;
	int val = 0;

	result = sensor->ops->active(client, 0, 0);
	if (result) {
		dev_err(&client->dev, "%s:sensor active fail\n", __func__);
		return result;
	}
	sensor->status_cur = SENSOR_OFF;

	result = of_property_read_u32(np, "ps_measure_time", &ps_val);
	if (result) {
		ps_val = TIME_ALS_100MS_PS_100MS;
		dev_err(&client->dev, "%s:Unable to read als_measure_time\n", 	__func__);
	} else {
		ps_val = (ps_val == 50) ? TIME_ALS_100MS_PS_50MS : TIME_ALS_100MS_PS_100MS;
	}

	result = sensor_write_reg(client, MODE_CTRL, (unsigned char)ps_val);
	if (result) {
		dev_err(&client->dev, "%s:write MODE_CTRL fail\n", __func__);
		return result;
	}

	result = of_property_read_u32(np, "ps_threshold_low", &ps_val);
	if (result) {
		ps_val = 0;
		dev_err(&client->dev, "%s:Unable to read ps_threshold_low\n", __func__);
	}

	ps_threshold_low = ps_val;
	result = sensor_write_reg(client, PS_TL_MSB, (unsigned char)((ps_val >> 8) & 0xFF));
	if (result) {
		dev_err(&client->dev, "%s:write PS_TL_MSB fail\n", __func__);
		return result;
	}
	result = sensor_write_reg(client, PS_TL_LSB, (unsigned char)(ps_val & 0xFF));
	if (result) {
		dev_err(&client->dev, "%s:write PS_TL_LSB fail\n", __func__);
		return result;
	}

	result = of_property_read_u32(np, "ps_threshold_high", &ps_val);
	if (result) {
		ps_val = 0x0FFF;
		dev_err(&client->dev, "%s:Unable to read ps_threshold_high\n", __func__);
	}

	ps_threshold_high = ps_val;
	result = sensor_write_reg(client, PS_TH_MSB,(unsigned char)((ps_val >> 8) & 0xFF));
	if (result) {
		dev_err(&client->dev, "%s:write PS_TH_MSB fail\n", __func__);
		return result;
	}

	result = sensor_write_reg(client, PS_TH_LSB, (unsigned char)(ps_val & 0xFF));
	if (result) {
		dev_err(&client->dev, "%s:write PS_TH_LSB fail\n", __func__);
		return result;
	}

	result = of_property_read_u32(np, "ps_ctrl_gain", &ps_val);
	if (result) {
		val = PS_GAIN_X1;	
		dev_err(&client->dev, "%s:Unable to read ps_ctrl_gain\n", __func__);
	} else {
		val = (ps_val << 4);
	}

	result = sensor_write_reg(client, PS_CTRL, (unsigned char)val);
	if (result) {
		dev_err(&client->dev, "%s:write PS_CTRL fail\n", __func__);
		return result;
	}

	result = of_property_read_u32(np, "ps_led_current", &ps_val);
	if (result) {
		val = LED_CURRENT_100MA;
		dev_err(&client->dev, "%s:Unable to read ps_led_current\n", __func__);
	}

	result = of_property_read_u32(np, "als_ctrl_gain", &ps_val);
	if (result) {
		val |= ALS_DATA0_GAIN_X1 | ALS_DATA1_GAIN_X1;
		dev_err(&client->dev, "%s:Unable to read als_ctrl_gain\n", __func__);
	} else {
		val |= (ps_val << 2) | (ps_val << 4);
	}
		
	result = sensor_write_reg(client, ALS_PS_CTRL, (unsigned char)val);
	if (result) {
		dev_err(&client->dev, "%s:write ALS_PS_CTRL fail\n", __func__);
		return result;
	}

	val = sensor_read_reg(client, INTERRUPT);
	if (sensor->pdata->irq_enable)
		val |= INT_PS_EN;
	else
		val &= ~INT_PS_EN;
	result = sensor_write_reg(client, INTERRUPT, val);
	if (result) {
		dev_err(&client->dev, "%s:write INTERRUPT fail\n", __func__);
		return result;
	}

	val = sensor_read_reg(client, sensor->ops->id_reg);
	if (val != sensor->ops->id_data) {
		dev_err(&client->dev, "%s:ID check fail\n", __func__);
		return result;
	}

	return result;
}

static int rpr0521_get_ps_value(int ps)
{
	int index = 0;

	if ((ps > ps_threshold_high) && (val_flag == 0)) {
		index = 1;
		val_flag = 1;
	} else if ((ps < ps_threshold_low) && (val_flag == 1)) {
		index = 0;
		val_flag = 0;
	} else {
		index = -1;
	}

	return index;
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;
	int value = 0;
	char buffer[2] = { 0 };
	int index = 1;

	if (sensor->ops->read_len < 2) {
		dev_err(&client->dev, "%s:length is error, len=%d\n", __func__, sensor->ops->read_len);
		return -EINVAL;
	}

	buffer[0] = sensor->ops->read_reg;
	result = sensor_rx_data(client, buffer, sensor->ops->read_len);
	if (result) {
		dev_err(&client->dev, "%s:sensor read data fail\n", __func__);
		return result;
	}
	value = (buffer[1] << 8) | buffer[0];

	index = rpr0521_get_ps_value(value);
	if (index >= 0) {
		input_report_abs(sensor->input_dev, ABS_DISTANCE, index);
		input_sync(sensor->input_dev);
		dev_dbg(&client->dev, "%s sensor closed=%d\n", sensor->ops->name, index);
	}

	return result;
}

static struct sensor_operate psensor_rpr0521_ops = {
	.name			= "ps_rpr0521",
	.type			= SENSOR_TYPE_PROXIMITY,
	.id_i2c			= PROXIMITY_ID_RPR0521,
	.read_reg		= PS_DATA_LSB,
	.read_len		= 2,
	.id_reg			= MANUFACT_ID,
	.id_data		= 0xE0,
	.precision		= 16,
	.ctrl_reg		= MODE_CTRL,
	.int_status_reg	= INTERRUPT,
	.range			= { 100, 65535 },
	.brightness		= { 10, 255 },
	.trig			= SENSOR_UNKNOW_DATA,
	.active			= sensor_active,
	.init			= sensor_init,
	.report			= sensor_report_value,
};

static int proximity_rpr0521_probe(struct i2c_client *client,
				   const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &psensor_rpr0521_ops);
}

static int proximity_rpr0521_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &psensor_rpr0521_ops);
}

static const struct i2c_device_id proximity_rpr0521_id[] = {
	{ "ps_rpr0521", PROXIMITY_ID_RPR0521 },
	{}
};

static struct i2c_driver proximity_rpr0521_driver = {
	.probe = proximity_rpr0521_probe,
	.remove = proximity_rpr0521_remove,
	.shutdown = sensor_shutdown,
	.id_table = proximity_rpr0521_id,
	.driver = {
		.name = "proximity_rpr0521",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

module_i2c_driver(proximity_rpr0521_driver);

MODULE_AUTHOR("TED <mspark@prazen.co>");
MODULE_DESCRIPTION("rpr0521 proximity driver");
MODULE_LICENSE("GPL");
