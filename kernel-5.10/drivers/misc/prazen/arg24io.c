
/*
 *  arg24io.c - System I/O setting.
 *
 *  Copyright (C) 2024 PRAZEN Co., Ltd. 
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 *
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/irqdomain.h>
#include <linux/fb.h>
#include <asm/uaccess.h>
#include <linux/syscore_ops.h>

#include "types.h"

#define DEV_NAME			"arg24io"
#define DRIVER_VERSION		"1.0"

static DEFINE_MUTEX(sysfs_lock); 

#define PATH_PANEL		"/sys/bus/i2c/devices/3-004c/display"

// gpio port
typedef struct 
{
	int	panel_reset;	// panel reset		
	int	lt_reset;		// lontium controller reset
} AR_IOCFG;

static AR_IOCFG ar_cfg;
static int g_sleep = 0;

///////////////////////////////////////////////////////////////////////////////////////////////////
int _atoi(const char *s)
{
	int i = 0;

	while (*s != '\0' && *s >= '0' && *s <= '9') {
        i = 10 * i + (*s - '0');
        s++;
	}
	return i;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void ar_drv_sleep(char *path, int val)
{
	char buf[4];
	mm_segment_t fs;
	struct file *fp;

	fs = get_fs();
	set_fs(KERNEL_DS);
	
	fp = filp_open(path, O_RDWR, 0664);
	if (IS_ERR(fp)) {
		printk("%s: file open error\n", __FUNCTION__);
        return;
	}
	
	if (fp) {
		loff_t f_pos = 0;
		int len = sizeof(buf);
		sprintf(buf, "%d\n", val);
		if (vfs_write(fp, buf, len, &f_pos) != len) {
			printk("%s: wrtie error\n", __FUNCTION__);
		}
		filp_close(fp, NULL);
		set_fs(fs);	
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////
#define AR_IO_RW(_name, gpio) \
static ssize_t _name##_gpio_show(struct device *dev, \
			struct device_attribute *attr, \
			char *buf) \
{ \
	ssize_t status; \
	int state ; \
	if (buf == NULL) return 0; \
	mutex_lock(&sysfs_lock); \
	state = gpio_get_value(gpio); \
	status = sprintf(buf, "%d\n", state); \
	mutex_unlock(&sysfs_lock); \
	return status; \
} \
static ssize_t _name##_gpio_store(struct device *dev, \
			struct device_attribute *attr, \
			const char *buf, size_t count) \
{ \
	int state; \
	if (buf == NULL) return count; \
	mutex_lock(&sysfs_lock); \
	sscanf(buf, "%d", &state); \
	gpio_set_value(gpio, state); \
	mutex_unlock(&sysfs_lock); \
	return count; \
} \
static DEVICE_ATTR(_name, 0660, _name##_gpio_show, _name##_gpio_store);

AR_IO_RW(panel_reset, ar_cfg.panel_reset);		// panel reset	
AR_IO_RW(lt_reset, ar_cfg.lt_reset);	// panel reset

static struct attribute *ar_io_attributes[] = {
	&dev_attr_panel_reset.attr,	
	&dev_attr_lt_reset.attr,	
	0
};

static struct attribute_group ar_io_attribute_group = {
	.name = NULL,
	.attrs = ar_io_attributes,
};


static const struct of_device_id ar_id_table[] = {
	{
	 .compatible = "prazen,arg24io",
	 },
	{},
};

////////////////////////////////////////////////////////////////////////////////////////////////////
static int ar_io_parse_dt(struct platform_device *pdev)
{
	int err;
	int gpio;
	struct device_node *np = pdev->dev.of_node;
	enum of_gpio_flags flags;
	const struct of_device_id *match;

	memset(&ar_cfg, 0, sizeof(AR_IOCFG));

	match = of_match_device(ar_id_table, &pdev->dev);
	if (!match) {
		dev_err(&pdev->dev,"Failed to find matching dt id\n");
		return -EINVAL;
	}

	gpio = of_get_named_gpio_flags(np, "panel_reset", 0, &flags);
	ar_cfg.panel_reset = gpio;
	if (gpio_is_valid(gpio)) {
		err = devm_gpio_request(&pdev->dev, gpio, "panel_reset");
		if (err) {
			dev_err(&pdev->dev, "failed to request GPIO%d : %d\n",gpio, __LINE__);
			return err;
		}		
		gpio_direction_output(gpio, (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1);
	}
	
	gpio = of_get_named_gpio_flags(np, "lt_reset", 0, &flags);
	ar_cfg.lt_reset = gpio;
	if (gpio_is_valid(gpio)) {
		err = devm_gpio_request(&pdev->dev, gpio, "lt_reset");
		if (err) {
			dev_err(&pdev->dev, "failed to request GPIO%d : %d\n",gpio, __LINE__);
			return err;
		}		
		gpio_direction_output(gpio, (flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1);
	}

	return 0;	
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static void arg24_sleep(int val)
{
	ar_drv_sleep(PATH_PANEL, val ? 0 : 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
static int oe_fb_event_notify(struct notifier_block *self,
					   unsigned long action,
					   void *data)
{
	struct fb_event *event = data;
	int mode = *((int *)event->data);

	if (mode == FB_BLANK_POWERDOWN) {
		if (!g_sleep) {
			g_sleep = 1;
			arg24_sleep(1);
		}
	} else {
		if (g_sleep) {
			g_sleep = 0;
			arg24_sleep(0);
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block oe_fb_notifier = {
	.notifier_call = oe_fb_event_notify,
};

static int ar_io_probe(struct platform_device *pdev)
{
	int ret = 0;

	ar_io_parse_dt(pdev);

	ret = sysfs_create_group(&pdev->dev.kobj, &ar_io_attribute_group);
	if (ret) {
		dev_err(&pdev->dev, "sysfs init failed. error=%d\n", ret);
		return ret;
	}
	
	fb_register_client(&oe_fb_notifier);

	printk("[%s] driver initialized\n", DEV_NAME);

	return ret;
}

static int ar_io_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj,  &ar_io_attribute_group);
	return 0;
}

static struct platform_driver ar_io_driver = {
	.probe = ar_io_probe,
	.remove = ar_io_remove,
	.driver = {
		   .name = DEV_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(ar_id_table),
		   },
};


static int __init ar_io_init(void)
{
	int ret = 0;

	ret = platform_driver_register(&ar_io_driver);
	return ret;
}

static void __exit ar_io_exit(void)
{
	platform_driver_unregister(&ar_io_driver);	
	printk("[%s] driver released\n", DEV_NAME);
}

module_init(ar_io_init);
module_exit(ar_io_exit);

MODULE_DESCRIPTION("GPIO Port Setup");
MODULE_AUTHOR("TED, <mspark@prazen.co.kr>");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

