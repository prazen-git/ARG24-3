/*
 *  sy060ldm01.c - SeeYA OLED 0.6' panel driver
 *
 *  Copyright (C) 2024 Prazen Co., Ltd. 
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/fb.h>

#include "types.h"
#include "sy060ldm01.h"

#define SY060_DEV_NAME		"sy060"
#define DRIVER_VERSION		"1.0"

#define MAX_BRIGHTNESS		(0x7A)

typedef enum
{
	flip_normal,
	flip_horizontal,
	flip_vertical,
	flip_all,
	flip_max
} FLIP_DATA;

//////////////////////////////////////////////////////////////////////////////
struct sy060_data {
	struct i2c_client *client;
	struct mutex update_lock;
	struct delayed_work SY060_work;
};
struct sy060_data *g_data;

typedef struct
{
	int	display;		// display on/off
	int	brightness;		// brightness
	int	sleep;			// sleep flag
}SY060_CONFIG;
static SY060_CONFIG s06_cfg;

///////////////////////////////////////////////////////////////////////////////////////////////////
static int sy060_write(struct i2c_client *client, u16 reg, u8 val)
{
	int err, cnt;
	u8 data[3];
	struct i2c_msg msg;

	data[0] = (u8)((reg >> 8) & 0xFF);
	data[1] = (u8)(reg & 0xFF);
	data[2] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = data;
	msg.len = val;

	cnt = 3;
	err = -EAGAIN;

	while ((cnt-- > 0) && (err < 0)) {
		err = i2c_transfer(client->adapter, &msg, 1);

		if (err >= 0) {
			err = 0;
			goto out;
		} else {
			printk(KERN_ERR"\n %s write (reg:0x%04x) failed, try to write again!\n", SY060_DEV_NAME, reg);
			udelay(10);
		}
	}

out:
	return err;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static u8 sy060_read(struct i2c_client *client, u16 reg)
{
	int err, cnt;
	u8 val = 0;
	u8 data[3];
	struct i2c_msg msg[2];

	data[0] = (u8)((reg >> 8) & 0xFF);
	data[1] = (u8)(reg & 0xFF);

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = &data[0];
	msg[0].len = 2;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = &data[2];
	msg[1].len = 1;

	cnt = 3;
	err = -EAGAIN;
	while ((cnt-- > 0) && (err < 0)) {
		err = i2c_transfer(client->adapter, msg, 2);
		if (err >= 0) {
			val = data[2];
			break;
		} else {
			printk("\n %s read (reg:0x%04x) failed, try to read again! \n", SY060_DEV_NAME, reg);
			udelay(10);
		}
	}

	return val;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int get_nits(u8 val) 
{
	int nits = 0;
	if (1 == val) {
		nits = 300;	// 0x01
	} else if (2 <= val && val <= 9) {
		nits = ((200 * 10) / 9 * (val - 1) / 10) + 300;
	} else if (10 == val) {
		nits = 500;
	} else if (11 <= val && val <= 19) {
		nits = (200 / 10 * (val - 10)) + 500;
	} else if (20 == val) {
		nits = 700;		
	} else if (21 <= val && val <= 29) {
		nits = (200 / 10 * (val - 20)) + 500;	
	} else if (30 == val) {
		nits = 900;	
	} else if (31 <= val && val <= 103) {
		nits = ((1700 * 100) / 74 * (val - 30) / 100) + 900;	
	} else if (104 == val) {
		nits = 2600;			
	} else if (105 <= val && val <= 179) {
		nits = ((1700 * 100) / 76 * (val - 104) / 100) + 2600;	
	}
	return nits;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int sy060_brightness(struct i2c_client *client, u8 val)
{
	if (val > MAX_BRIGHTNESS) {
		printk("[Panel] set brightness error %d\n", val);
		return 0;
	}
	printk("[Panel] set brightness %d = nits %d\n", val, get_nits(val));
	return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int sy060_rotate(struct i2c_client *client, u8 val)
{
	u8 flip = 0x00;
		
	if (val > flip_max) {
		printk("[Panel] set rotate error %d\n", val);
		return 0;
	}
	printk("[Panel] set rotate = %d\n", val);

	switch (val) {
		case flip_horizontal:	flip = 0x02;	break;
		case flip_vertical:		flip = 0x01;	break;
		case flip_all:			flip = 0x03;	break;
        default:    break;
	}
	sy060_write(client, SY_FLIP_REG, flip);
	return 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
static ssize_t sy060_disp_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (buf == NULL)
		return 0;
	printk("[Panel] Get Display %s\n", s06_cfg.display ? "On" : "Off");
	return sprintf(buf, "%d\n", s06_cfg.display);
}

static ssize_t sy060_disp_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	int val = _atoi(buf);
	if (buf == NULL)
		return count;
	printk("[Panel] Set Display %s\n", val ? "On" : "Off");
	sy060_write(client, (val ? SY_DISP_ON_REG : SY_DISP_OFF_REG), 0x00);
	s06_cfg.display = val;
	return count;
}
static DEVICE_ATTR(display, 0660, sy060_disp_show, sy060_disp_store);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
static ssize_t sy060_brightness_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (buf == NULL)
		return 0;
	printk("[Panel] Get Brightness %d\n", s06_cfg.brightness);
	return sprintf(buf, "%d\n", s06_cfg.brightness);
}

static ssize_t sy060_brightness_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	int val = _atoi(buf);
	if (buf == NULL)
		return count;
	printk("[Panel] Set Brightness %d\n", val);
	sy060_brightness(client, val);
	s06_cfg.brightness = val;
	return count;
}
static DEVICE_ATTR(brightness, 0660, sy060_brightness_show, sy060_brightness_store);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////	
static ssize_t sy060_rotate_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (buf == NULL)
		return 0;
	printk("[Panel] Get Brightness %d\n", s06_cfg.brightness);
	return sprintf(buf, "%d\n", s06_cfg.brightness);
}

static ssize_t sy060_rotate_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	int val = _atoi(buf);
	if (buf == NULL)
		return count;
	printk("[Panel] Set Rotate %d\n", val);
	sy060_rotate(client, val);
	s06_cfg.brightness = val;
	return count;
}
static DEVICE_ATTR(rotate, 0660, sy060_rotate_show, sy060_rotate_store);
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static struct attribute *sy060_attributes[] = {
	&dev_attr_display.attr,
	&dev_attr_brightness.attr,	
	&dev_attr_rotate.attr,	
	NULL
};

static const struct attribute_group sy060_attr_group = {
	.attrs = sy060_attributes,
};

static struct of_device_id sy060_dt_ids[] = {
	{ .compatible = "seeya,sy060" },
	{},
};
MODULE_DEVICE_TABLE(of, sy060_dt_ids);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int sy060_init_client(struct i2c_client *client)
{
	int ret = sy060_write(client, 0xFF00, 0x5A);
	if (ret < 0) {
		goto exit;
	}
	printk("[sy102] check i2c = %02X\n", sy060_read(client, 0xFF00));

 	sy060_write(client, 0xFF01, 0x81);
 	sy060_write(client, 0xF406, 0x55);
 	sy060_write(client, 0x5300, 0x24);
 	sy060_write(client, 0x5100, 0xFF);
 	sy060_write(client, 0x5101, 0x00);
 	sy060_write(client, 0x0300, 0x00);
 	sy060_write(client, 0x8000, 0x01);
 	sy060_write(client, 0x8001, 0xE0);
 	sy060_write(client, 0x8002, 0xE0);
 	sy060_write(client, 0x8003, 0x0E);
 	sy060_write(client, 0x8004, 0x00);
 	sy060_write(client, 0x8005, 0x31);
 	sy060_write(client, 0x8100, 0x04);
 	sy060_write(client, 0x8101, 0x82);
 	sy060_write(client, 0x8102, 0x00);
 	sy060_write(client, 0x8103, 0x10);
 	sy060_write(client, 0x8104, 0x00);
 	sy060_write(client, 0x8105, 0x10);
 	sy060_write(client, 0x8106, 0x00);
 	sy060_write(client, 0x8107, 0x04);
 	sy060_write(client, 0x8108, 0x82);
 	sy060_write(client, 0x8109, 0x00);
 	sy060_write(client, 0x810A, 0x10);
 	sy060_write(client, 0x810B, 0x00);
 	sy060_write(client, 0x810C, 0x10);
 	sy060_write(client, 0x810D, 0x00);
 	sy060_write(client, 0x810E, 0x04);
 	sy060_write(client, 0x810F, 0x82);
 	sy060_write(client, 0x8110, 0x00);
 	sy060_write(client, 0x8111, 0x10);
 	sy060_write(client, 0x8112, 0x00);
 	sy060_write(client, 0x8113, 0x10);
 	sy060_write(client, 0x8114, 0x00);
 	sy060_write(client, 0x6C00, 0x00);
 	sy060_write(client, 0x3500, 0x00);
 	sy060_write(client, 0x2600, 0x20);
 	sy060_write(client, 0xFF00, 0x5A);
 	sy060_write(client, 0xFF01, 0x80);
 	sy060_write(client, 0xF249, 0x01);
 	sy060_write(client, 0xFF00, 0x5A);
 	sy060_write(client, 0xFF01, 0x81);
 	sy060_write(client, 0xF61D, 0x30);
 	sy060_write(client, 0xF429, 0x04);
 	
 	sy060_write(client, 0xF000, 0xAA);
 	sy060_write(client, 0xF001, 0x10);
 	sy060_write(client, 0xB102, 0x09);
 	
	sy060_write(client, 0x1100, 0x00);
	mdelay(100);
	sy060_write(client, 0x2900, 0x00);
	
	return 1;

exit:
	printk("%s: error ret = %d\n", __func__, ret);
	return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int sy060_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_I2C)) {
		err = -EIO;
		goto exit;
	}

	g_data = kzalloc(sizeof(struct sy060_data), GFP_KERNEL);
	if (!g_data) {
		err = -ENOMEM;
		goto exit;
	}
	g_data->client = client;
	i2c_set_clientdata(client, g_data);

	mutex_init(&g_data->update_lock);

	memset(&s06_cfg, 0, sizeof(s06_cfg));
	
	s06_cfg.display = 1;
	s06_cfg.brightness = 4;

	sy060_init_client(client);

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &sy060_attr_group);
	if (err)
		goto exit_kfree;

	dev_info(&client->dev, "support ver. %s enabled\n", DRIVER_VERSION);

	return 0;

exit_kfree:
	kfree(g_data);
exit:
	return err;
}

static int sy060_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &sy060_attr_group);
	kfree(i2c_get_clientdata(client));

	return 0;
}

static const struct i2c_device_id sy060_id[] = {
	{"sy060", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sy060_id);

static struct i2c_driver sy060_driver = {
	.driver = {
		.name = SY060_DEV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(sy060_dt_ids),		   
	},
	.probe = sy060_probe,
	.remove = sy060_remove,
	.id_table = sy060_id,
};

static int __init sy060_init(void)
{
	return i2c_add_driver(&sy060_driver);
}

static void __exit sy060_exit(void)
{
	i2c_del_driver(&sy060_driver);
}

MODULE_DESCRIPTION("SeeYA OLED 0.6inch Panel");
MODULE_AUTHOR("TED, <mspark@prazen.co>");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

module_init(sy060_init);
module_exit(sy060_exit);
