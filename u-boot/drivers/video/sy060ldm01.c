/*
 * (C) Copyright 2024 Prazen Co., Ltd
 *
 * sy060ldm01.c -- SeeYA OLED Panel driver
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <common.h>
#include <dm.h>
#include <i2c.h>
#include <asm/gpio.h>

#define COMPAT_SY060			"seeya,sy060"

struct sy060_dev {
	struct udevice	*dev;
	struct gpio_desc reset_gpio;
};

struct oled_panel_funcs {
	void (*disp_on)(struct sy060_dev *dev);
	void (*disp_off)(struct sy060_dev *dev);
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
static u8 sy060_read(struct sy060_dev *dev, uint reg)
{
	u8 val;
	int ret = dm_i2c_read(dev->dev, reg, &val, 1);
	if (ret) {
		printf("[sy060] i2c read failed (%04X) : %d\n", reg, ret);
		return ret;
	}
	return val;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
static int sy060_write(struct sy060_dev *dev, uint reg, u8 data)
{
	int ret = dm_i2c_write(dev->dev, reg, &data, 1);
	if (ret) {
		printf("[sy060] i2c write failed (%04X) : %d\n", reg, ret);
		return 0;
	}
	return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
static void sy060_disp_on(struct sy060_dev *dev)
{
	sy060_write(dev, 0x2900, 0x00);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
static void sy060_disp_off(struct sy060_dev *dev)
{
	sy060_write(dev, 0x2800, 0x00);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
int sy060_device_init(struct sy060_dev *dev)
{
	if (sy060_write(dev, 0xFF00, 0x5A) == 0) {
		printf("[sy060] reg init failed\n");
		return 0;
	}
	printf("[sy102] check i2c = %02X\n", sy060_read(dev, 0xFF00));

 	sy060_write(dev, 0xFF01, 0x81);
 	sy060_write(dev, 0xF406, 0x55);
 	sy060_write(dev, 0x5300, 0x24);
 	sy060_write(dev, 0x5100, 0xFF);
 	sy060_write(dev, 0x5101, 0x00);
 	sy060_write(dev, 0x0300, 0x00);
 	sy060_write(dev, 0x8000, 0x01);
 	sy060_write(dev, 0x8001, 0xE0);
 	sy060_write(dev, 0x8002, 0xE0);
 	sy060_write(dev, 0x8003, 0x0E);
 	sy060_write(dev, 0x8004, 0x00);
 	sy060_write(dev, 0x8005, 0x31);
 	sy060_write(dev, 0x8100, 0x04);
 	sy060_write(dev, 0x8101, 0x82);
 	sy060_write(dev, 0x8102, 0x00);
 	sy060_write(dev, 0x8103, 0x10);
 	sy060_write(dev, 0x8104, 0x00);
 	sy060_write(dev, 0x8105, 0x10);
 	sy060_write(dev, 0x8106, 0x00);
 	sy060_write(dev, 0x8107, 0x04);
 	sy060_write(dev, 0x8108, 0x82);
 	sy060_write(dev, 0x8109, 0x00);
 	sy060_write(dev, 0x810A, 0x10);
 	sy060_write(dev, 0x810B, 0x00);
 	sy060_write(dev, 0x810C, 0x10);
 	sy060_write(dev, 0x810D, 0x00);
 	sy060_write(dev, 0x810E, 0x04);
 	sy060_write(dev, 0x810F, 0x82);
 	sy060_write(dev, 0x8110, 0x00);
 	sy060_write(dev, 0x8111, 0x10);
 	sy060_write(dev, 0x8112, 0x00);
 	sy060_write(dev, 0x8113, 0x10);
 	sy060_write(dev, 0x8114, 0x00);
 	sy060_write(dev, 0x6C00, 0x00);
 	sy060_write(dev, 0x3500, 0x00);
 	sy060_write(dev, 0x2600, 0x20);
 	sy060_write(dev, 0xFF00, 0x5A);
 	sy060_write(dev, 0xFF01, 0x80);
 	sy060_write(dev, 0xF249, 0x01);
 	sy060_write(dev, 0xFF00, 0x5A);
 	sy060_write(dev, 0xFF01, 0x81);
 	sy060_write(dev, 0xF61D, 0x30);
 	sy060_write(dev, 0xF429, 0x04);
 	
 	sy060_write(dev, 0xF000, 0xAA);
 	sy060_write(dev, 0xF001, 0x10);
 	sy060_write(dev, 0xB102, 0x09);
 	
	sy060_write(dev, 0x1100, 0x00);
	mdelay(100);
	sy060_write(dev, 0x2900, 0x00);
	
	return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
static int sy060_probe(struct udevice *dev)
{
	struct sy060_dev *s_dev = dev_get_priv(dev);
	const void *blob = gd->fdt_blob;
	int node = fdt_node_offset_by_compatible(blob, 0, COMPAT_SY060);
	int ret = gpio_request_by_name(dev, "reset-gpio", 0, &s_dev->reset_gpio, GPIOD_IS_OUT);

	if (node < 0) {
		printf("Can't find dts node for sy060\n");
		return -ENODEV;
	}

	if (ret) {
		printf("Cannot get Reset GPIO: %d\n", ret);
		return ret;
	}

	s_dev->dev = dev;

	// reset 'high'
	dm_gpio_set_value(&s_dev->reset_gpio, 1);

	return sy060_device_init(s_dev);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
static const struct oled_panel_funcs sy060_ops = {
	.disp_on	= sy060_disp_on,
	.disp_off	= sy060_disp_off,		
};

static const struct udevice_id sy060_of_match[] = {
	{ .compatible = COMPAT_SY060 },
	{ }
};

U_BOOT_DRIVER(sy060_drv) = {
	.name		= "sy060",
	.id			= UCLASS_I2C_GENERIC,
	.of_match	= sy060_of_match,
	.ops		= &sy060_ops,
	.probe		= sy060_probe,
	.priv_auto_alloc_size	= sizeof(struct sy060_dev),
};
