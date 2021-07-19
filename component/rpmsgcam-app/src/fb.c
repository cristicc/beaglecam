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
static int fbfd = -1;
static char *fbp = 0;

/*
 * Initialize frame buffer.
 */
int init_fb(const char *dev_path)
{
	long int screen_size = 0;
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

	/* Compute the screen size (bytes) */
	screen_size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;

	if (vinfo.bits_per_pixel != 16) {
		log_error("Expected 16 bpp, but found: %s", vinfo.bits_per_pixel);
		ret = -1;
		goto fail;
	}

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

/*
 * Write RGB565 pixel data into the frame buffer.
 * TODO: provide source image size and scale to FB screen size
 */
void write_fb(uint16_t *rgb565)
{
	int x, y;

	if (fbfd < 0)
		return;

	for (y = 0; y < 120; y++)
		for (x = 0; x < 160; x++)
			*((uint16_t *)(fbp + y * finfo.line_length + x * 2)) = rgb565[y * 160 + x];
}

/*
 * Release frame buffer resources.
 */
void release_fb()
{
	if (fbfd < 0)
		return;

	munmap(fbp, vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8);
	close(fbfd);
	fbfd = -1;
}
