/*
 * Utility to display RGB565 image content via Frame Buffer.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _FB_H
#define _FB_H

#include <stdint.h>

int fb_init(const char *dev_path);
void fb_write(uint16_t *rgb565, int xres, int yres);
void fb_clear();
void fb_release();

#endif /* _FB_H */
