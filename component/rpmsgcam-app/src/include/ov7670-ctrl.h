/*
 * Utility to setup OV7670 camera module via the SCCB (I2C-like) interface.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _OV7670_CTRL_H
#define _OV7670_CTRL_H

int cam_init(const char *i2c_dev);

#endif /* _OV7670_CTRL_H */
