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
#define	ALS_DATA0_LSB		0x46
#define	ALS_DATA0_MSB		0x47
#define	ALS_DATA1_LSB		0x48
#define	ALS_DATA1_MSB		0x49
#define	INTERRUPT			0x4A
#define	ALS_DATA0_TH_LSB	0x4F
#define	ALS_DATA0_TH_MSB	0x50
#define	ALS_DATA0_TL_LSB	0x51
#define	ALS_DATA0_TL_MSB	0x52
#define	ALS_OFFSET_LSB		0x53
#define	ALS_OFFSET_MSB		0x54
#define	MANUFACT_ID			0x92

/* MODE_CTRL (0x41) */
#define	ALS_DIS				(0 << 7)
#define	ALS_EN				(1 << 7)
// measurement itme
#define TIME_ALS_100MS_PS_50MS	(0x05)
#define TIME_ALS_100MS_PS_100MS	(0x06)

/* LED_CTRL (0x42) */
#define ALS_DATA0_GAIN_X1	(0 << 2)
#define ALS_DATA1_GAIN_X1	(0 << 4)
#define LED_CURRENT_100MA	(2 << 0)

/* INTERRUPT (0x4A) */
#define ALS_INT_STATUS		(1 << 6)
#define INT_ALS_EN			(2 << 0)
#define INT_PS_ALS_EN		(3 << 0)


static int sensor_active(struct i2c_client *client, int enable, int rate)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;
	int status = 0;

	sensor->ops->ctrl_data = sensor_read_reg(client, sensor->ops->ctrl_reg);
	if (!enable) {
		status = ~ALS_EN;
		sensor->ops->ctrl_data &= status;
	} else {
		status |= ALS_EN;
		sensor->ops->ctrl_data |= status;
	}

	dev_dbg(&client->dev, "reg=0x%x, reg_ctrl=0x%x, enable=%d\n", sensor->ops->ctrl_reg, sensor->ops->ctrl_data, enable);

	result = sensor_write_reg(client, sensor->ops->ctrl_reg,  sensor->ops->ctrl_data);
	if (result)
		dev_err(&client->dev, "%s:fail to active sensor\n", __func__);

	return result;
}

static int sensor_init(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	struct device_node *np = client->dev.of_node;
	int als_val = 0;
	int val = 0;
	int ret = 0;

	ret = sensor->ops->active(client, 0, 0);
	if (ret) {
		dev_err(&client->dev, "%s:sensor active fail\n", __func__);
		return ret;
	}
	sensor->status_cur = SENSOR_OFF;

	ret = of_property_read_u32(np, "als_measure_time", &als_val);
	if (ret) {
		als_val = TIME_ALS_100MS_PS_100MS;
		dev_err(&client->dev, "%s:Unable to read als_measure_time\n", 	__func__);
	} else {
		als_val = TIME_ALS_100MS_PS_100MS;
	}

	ret = sensor_write_reg(client, MODE_CTRL, (unsigned char)als_val);
	if (ret) {
		dev_err(&client->dev, "%s:write MODE_CTRL fail\n", __func__);
		return ret;
	}

	ret = of_property_read_u32(np, "als_threshold_low", &als_val);
	if (ret) {
		als_val = 0;
		dev_err(&client->dev, "%s:Unable to read als_threshold_low\n", 	__func__);
	}
	ret = sensor_write_reg(client, ALS_DATA0_TL_MSB, (unsigned char)((als_val >> 8) & 0xFF));
	if (ret) {
		dev_err(&client->dev, "%s:write ALS_DATA0_TL_MSB fail\n", __func__);
		return ret;
	}

	ret = sensor_write_reg(client, ALS_DATA0_TL_LSB, (unsigned char)(als_val & 0xFF));
	if (ret) {
		dev_err(&client->dev, "%s:write ALS_DATA0_TL_LSB fail\n", __func__);
		return ret;
	}

	ret = of_property_read_u32(np, "als_threshold_high", &als_val);
	if (ret) {
		als_val = 0xFFFF;
		dev_err(&client->dev, "%s:Unable to read als_threshold_high\n", __func__);
	}

	ret = sensor_write_reg(client, ALS_DATA0_TH_MSB, (unsigned char)((als_val >> 8) & 0xFF));
	if (ret) {
		dev_err(&client->dev, "%s:write ALS_DATA0_TH_MSB fail\n", __func__);
		return ret;
	}

	ret = sensor_write_reg(client, ALS_DATA0_TH_LSB, (unsigned char)(als_val & 0xFF));
	if (ret) {
		dev_err(&client->dev, "%s:write ALS_DATA0_TH_LSB fail\n", __func__);
		return ret;
	}

	ret = of_property_read_u32(np, "als_ctrl_gain", &als_val);
	if (ret) {
		val = ALS_DATA0_GAIN_X1 | ALS_DATA1_GAIN_X1;
		dev_err(&client->dev, "%s:Unable to read als_ctrl_gain\n", __func__);
	} else {
		val = (als_val << 2) | (als_val << 4);
	}
	
	ret = of_property_read_u32(np, "ps_led_current", &als_val);
	if (ret) {
		val |= LED_CURRENT_100MA;
		dev_err(&client->dev, "%s:Unable to read ps_led_current\n", __func__);
	} else {
		val |= als_val;
	}

	ret = sensor_write_reg(client, ALS_PS_CTRL, (unsigned char)val);
	if (ret) {
		dev_err(&client->dev, "%s:write ALS_CTRL fail\n", __func__);
		return ret;
	}

	val = sensor_read_reg(client, INTERRUPT);
	if (sensor->pdata->irq_enable)
		val |= INT_ALS_EN;
	else
		val &= ~INT_ALS_EN;
	ret = sensor_write_reg(client, INTERRUPT, (unsigned char)val);
	if (ret) {
		dev_err(&client->dev, "%s:write INTERRUPT fail\n", __func__);
		return ret;
	}

	val = sensor_read_reg(client, sensor->ops->id_reg);
	if (val != sensor->ops->id_data) {
		dev_err(&client->dev, "%s:ID check fail\n", __func__);
		return ret;
	}

	return ret;
}

static int light_report_value(struct input_dev *input, int data)
{
	unsigned char index = 0;

	if (data <= 100) {
		index = 0;
		goto report;
	} else if (data <= 1600) {
		index = 1;
		goto report;
	} else if (data <= 2250) {
		index = 2;
		goto report;
	} else if (data <= 3200) {
		index = 3;
		goto report;
	} else if (data <= 6400) {
		index = 4;
		goto report;
	} else if (data <= 12800) {
		index = 5;
		goto report;
	} else if (data <= 26000) {
		index = 6;
		goto report;
	} else {
		index = 7;
		goto report;
	}

report:
	input_report_abs(input, ABS_MISC, index);
	input_sync(input);
	return index;
}

static int sensor_report_value(struct i2c_client *client)
{
	struct sensor_private_data *sensor =
		(struct sensor_private_data *)i2c_get_clientdata(client);
	int result = 0;
	int value = 0;
	int index = 0;
	char buffer[2] = { 0 };

	if (sensor->ops->read_len < 2) {
		dev_err(&client->dev, "%s:length is error, len=%d\n", __func__,
			sensor->ops->read_len);
		return -EINVAL;
	}

	buffer[0] = sensor->ops->read_reg;
	result = sensor_rx_data(client, buffer, sensor->ops->read_len);
	if (result) {
		dev_err(&client->dev, "%s:sensor read data fail\n", __func__);
		return result;
	}
	value = (buffer[1] << 8) | buffer[0];
	index = light_report_value(sensor->input_dev, value);
	dev_dbg(&client->dev, "%s result=0x%x, index=%d\n", sensor->ops->name, value, index);
/*
	if (sensor->pdata->irq_enable && sensor->ops->int_status_reg) {
		value = sensor_read_reg(client, sensor->ops->int_status_reg);
		if (value & ALS_INT_STATUS) {
			value &= ~ALS_INT_STATUS;
			result = sensor_write_reg(client, sensor->ops->int_status_reg, value);
			if (result) {
				dev_err(&client->dev, "write status reg error\n");
				return result;
			}
		}
	}
*/
	return result;
}

static struct sensor_operate light_rpr0521_ops = {
	.name			= "ls_rpr0521",
	.type			= SENSOR_TYPE_LIGHT,	//sensor type and it should be correct
	.id_i2c			= LIGHT_ID_RPR0521,		//i2c id number
	.read_reg		= ALS_DATA0_LSB,
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

static int light_rpr0521_probe(struct i2c_client *client, const struct i2c_device_id *devid)
{
	return sensor_register_device(client, NULL, devid, &light_rpr0521_ops);
}

static int light_rpr0521_remove(struct i2c_client *client)
{
	return sensor_unregister_device(client, NULL, &light_rpr0521_ops);
}

static const struct i2c_device_id light_rpr0521_id[] = {
	{ "ls_rpr0521", LIGHT_ID_RPR0521 },
	{}
};

static struct i2c_driver light_rpr0521_driver = {
	.probe = light_rpr0521_probe,
	.remove = light_rpr0521_remove,
	.shutdown = sensor_shutdown,
	.id_table = light_rpr0521_id,
	.driver = {
		.name = "light_rpr0521",
#ifdef CONFIG_PM
		.pm = &sensor_pm_ops,
#endif
	},
};

module_i2c_driver(light_rpr0521_driver);

MODULE_AUTHOR("TED <mspark@prazen.co>");
MODULE_DESCRIPTION("rpr0521 light driver");
MODULE_LICENSE("GPL");
