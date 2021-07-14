#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "fb.h"
#include "log.h"

static int fbfd = 0;
static struct fb_var_screeninfo vinfo;
static struct fb_fix_screeninfo finfo;
static char *fbp = 0;

/*
 * Initialize frame buffer
 */
int init_fb(char* fb_path)
{
	long int screen_size = 0;
	int ret;

	fbfd = open(fb_path, O_RDWR);
	if (fbfd < 0) {
		log_fatal("Failed opening FB %s: ", strerror(errno));
		return fbfd;
	}

	/* Get fixed screen information */
	ret = ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
	if (ret < 0) {
		log_fatal("Failed reading fixed FB info: %s", strerror(errno));
		goto fail;
	}

	/* Get variable screen information */
	ret = ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);
	if (ret < 0) {
		log_fatal("Failed reading variable FB info: %s", strerror(errno));
		goto fail;
	}

	log_info("FB: %dx%d, %dbpp, length %d, offsets: %d %d",
			 vinfo.xres, vinfo.yres,
			 vinfo.bits_per_pixel, finfo.line_length,
			 vinfo.xoffset, vinfo.yoffset);

	/* Compute the screen size (bytes) */
	screen_size = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;

	/* Map device to memory */
	fbp = (char *)mmap(0, screen_size, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (fbp == MAP_FAILED) {
		ret = -1;
		log_fatal("Failed to map FB device to memory: %s", strerror(errno));
		goto fail;
	}

	return 0;

fail:
	close(fbfd);
	return ret;
}


/*
 * Write pixel data into the frame buffer.
 */
void update_fb(uint8_t* pixbuf)
{
	uint8_t* bP;
	int x, y, offset;

	for (y = 0; y < 240; y = y + 2) {
		/* Line 1 */
		bP = pixbuf + y / 2 *160;
		for (x = 0; x < 320; x += 2) {
			offset = (x * 2) + (y * finfo.line_length);
			*((uint16_t*)(fbp + offset)) = *(uint16_t*)bP;
			*((uint16_t*)(fbp + offset + 2)) = *(uint16_t*)bP;
		}

		/* Line 2 */
		bP = pixbuf + y / 2 * 160;
		for (x = 0; x < 320; x += 2) {
			offset = (x * 2) + ((y + 1) * finfo.line_length);
			*((uint16_t*)(fbp + offset)) = *(uint16_t*)bP;
			*((uint16_t*)(fbp + offset + 2)) = *(uint16_t*)bP;
		}
	}
}
