/*
 *  types.h - System header file.
 *
 *  Copyright (C) 2024 Prazen Co., Ltd. 
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 *
 */

#ifndef __TYPES_H__
#define __TYPES_H__

struct i2c_set_data_type {
	u16 reg;
	u16 data_len;			/* Burst data length */
	u8  data[64];			/* Data buff		 */
};	

extern int _atoi(const char *s);
extern struct file *file_open(char *path,int flag,int mode);
extern int file_read(struct file *fp, char *buf, int readlen);
extern int file_write(struct file *fp, char *buf, int writelen);
extern int file_close(struct file *fp);

#endif  /*__TYPES_H__*/
