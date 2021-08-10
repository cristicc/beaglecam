/*
 * GPIO utility.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "gpio-util.h"
#include "log.h"

/*
 * Configures the requested line in the specified GPIO chip as output port.
 * Returns the file descriptor for the requested GPIO line.
 *
 * For a quick check of the available GPIO lines, use the following commands:
 *
 *  $ mount -t debugfs debugfs /sys/kernel/debug
 *  $ cat /sys/kernel/debug/gpio
 *
 */
int gpioutil_line_request_output(const char *gpiochip_dev_path, int line_offset)
{
	struct gpiohandle_request req;
	struct gpioline_info linfo;
	int gpiochip_fd, ret;

	gpiochip_fd = open(gpiochip_dev_path, O_RDWR | O_CLOEXEC);
	if (gpiochip_fd < 0) {
		log_error("Failed to open %s: %s", gpiochip_dev_path, strerror(errno));
		return gpiochip_fd;
	}

	req.lineoffsets[0] = line_offset;
	req.flags = GPIOHANDLE_REQUEST_OUTPUT;
	req.default_values[0] = 0;
	strcpy(req.consumer_label, "rpmsgcam-app");
	req.lines = 1;

	ret = ioctl(gpiochip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req);
	if (ret < 0) {
		ret = -errno;
		log_error("GPIO_GET_LINEHANDLE_IOCTL failed: %s", strerror(errno));
		goto err_close;
	}

	memset(&linfo, 0, sizeof(linfo));
	linfo.line_offset = line_offset;

	ret = ioctl(gpiochip_fd, GPIO_GET_LINEINFO_IOCTL, &linfo);
	if (ret < 0) {
		ret = -errno;
		log_error("GPIO_GET_LINEINFO_IOCTL failed: %s", strerror(errno));
		goto err_close;
	}

	log_info("Configured GPIO line %d as output (line name: %s)", line_offset, linfo.name);
	ret = req.fd > 0 ? req.fd : -1;

err_close:
	if (close(gpiochip_fd) != 0)
		log_error("Failed to close GPIO device file: %s", strerror(errno));
	return ret;
}

/*
 * Writes data to a GPIO output line descriptor obtained
 * by calling gpioutil_line_request_output().
 */
int gpioutil_line_set_value(int gpioline_fd, int value)
{
	struct gpiohandle_data data;
	int ret;

	data.values[0] = value;

	ret = ioctl(gpioline_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
	if (ret < 0) {
		log_error("GPIOHANDLE_SET_LINE_VALUES_IOCTL failed: ", strerror(errno));
		return ret;
	}

	return 0;
}
