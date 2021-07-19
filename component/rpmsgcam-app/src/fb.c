/*
 * Utility to display RGB565 image content via Frame Buffer.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "fb.h"
#include "log.h"

/* TODO: typedef void *fb_handle_t; */
static struct fb_fix_screeninfo finfo;
static struct fb_var_screeninfo vinfo;
static uint32_t screen_size;
static int fbfd = -1;
static char *fbp = 0;

/*
 * Initialize frame buffer.
 */
int fb_init(const char *dev_path)
{
	int ret;

	fbfd = open(dev_path, O_RDWR);
	if (fbfd < 0) {
		log_error("Failed to open %s: %s", dev_path, strerror(errno));
		return fbfd;
	}

	/* Get fixed screen information */
	ret = ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
	if (ret < 0) {
		log_error("Failed reading fixed FB info: %s", strerror(errno));
		goto fail;
	}

	/* Get variable screen information */
	ret = ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);
	if (ret < 0) {
		log_error("Failed reading variable FB info: %s", strerror(errno));
		goto fail;
	}

	log_info("FB screen info: %dx%d, %dbpp, xoff=%d, yoff=%d",
			 vinfo.xres, vinfo.yres, vinfo.bits_per_pixel,
			 vinfo.xoffset, vinfo.yoffset);

	if (vinfo.bits_per_pixel != 16) {
		log_error("Expected 16 bpp, but found: %s", vinfo.bits_per_pixel);
		ret = -1;
		goto fail;
	}

	/* Compute the screen size (bytes) */
	screen_size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;

	/* Map device to memory */
	fbp = (char *)mmap(0, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (fbp == MAP_FAILED) {
		log_error("Failed to map FB device to memory: %s", strerror(errno));
		ret = -1;
		goto fail;
	}

	return 0;

fail:
	close(fbfd);
	fbfd = -1;
	return ret;
}

/**
 * Write RGB565 pixel data into the frame buffer.
 *
 * @rgb565: RGB565 pixel data in BGR (little endian) format
 * @xres: Pixel data X resolution
 * @yres: Pixel data Y resolution
 */
void fb_write(uint16_t *rgb565, int xres, int yres)
{
	int fb_xoff, fb_yoff, fb_xres, fb_yres;
	int x, y;

	if (fbfd < 0)
		return;

	fb_xoff = (vinfo.xres - xres) / 2;
	if (fb_xoff < 0) {
		fb_xoff = 0;
		fb_xres = vinfo.xres;
	} else {
		fb_xres = xres;
	}

	fb_yoff = (vinfo.yres - yres) / 2;
	if (fb_yoff < 0) {
		fb_yoff = 0;
		fb_yres = vinfo.yres;
	} else {
		fb_yres = yres;
	}

	for (y = 0; y < fb_yres; y++)
		for (x = 0; x < fb_xres; x++)
			((uint16_t *)(fbp))[(y + fb_yoff) * vinfo.xres
								+ x + fb_xoff] = rgb565[y * fb_xres + x];
}

/*
 * Writes a black frame.
 */
void fb_clear()
{
	memset(fbp, 0, screen_size);
}

/*
 * Release frame buffer resources.
 */
void fb_release()
{
	if (fbfd < 0)
		return;

	munmap(fbp, screen_size);
	close(fbfd);
	fbfd = -1;
}
