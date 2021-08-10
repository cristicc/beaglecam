/*
 * I2C utility.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _I2C_UTIL_H
#define _I2C_UTIL_H

int i2c_open(const char *dev_path, unsigned char addr);

int i2c_read(int fd, unsigned char addr, unsigned char *buf, unsigned int len);

int i2c_write(int fd, unsigned char addr, unsigned char *buf, unsigned int len);

int i2c_write_read(int fd, unsigned char addr,
				   unsigned char *buf_w, unsigned int len_w,
				   unsigned char *buf_r, unsigned int len_r);

#endif /* _I2C_UTIL_H */
