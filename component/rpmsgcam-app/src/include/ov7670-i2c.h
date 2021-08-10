/*
 * Utility to setup OV7670 camera module via the SCCB (I2C-like) interface.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _OV7670_I2C_H
#define _OV7670_I2C_H

int ov7670_i2c_setup(const char *dev_path);

#endif /* _OV7670_I2C_H */
