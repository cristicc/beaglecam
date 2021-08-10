/*
 * I2C utility.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "i2c-util.h"
#include "log.h"

/**
 * Opens an I2C device for R/W operations.
 *
 * @dev_path: I2C device path
 * @addr: 7-bit I2C slave address to be provided when intending to execute
 * 		  I2C transactions via read() and write() calls.
 * 		  Use 0xff to skip setting the address via I2C_SLAVE ioctl, useful for
 * 		  I2C transactions done via the i2c_{write,read,write_read} functions.
 *
 * Return: the I2C file descriptor on success or -errno on failure
 */
int i2c_open(const char *dev_path, unsigned char addr)
{
	int fd, ret;

	fd = open(dev_path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		log_error("Failed to open %s: %s", dev_path, strerror(errno));
		return fd;
	}

	if (addr == 0xff)
		return fd;

	ret = ioctl(fd, I2C_SLAVE, addr);
	if (ret < 0) {
		ret = -errno;
		log_error("I2C_SLAVE ioctl failed: %s", strerror(errno));
		goto err_close;
	}

	return fd;

err_close:
	if (close(fd) != 0)
		log_error("Failed to close i2c fd: %s", strerror(errno));
	return ret;
}

/**
 * Reads data from an I2C device.
 *
 * @fd: I2C file descriptor
 * @addr: I2C slave address
 * @buf: buffer to store received data
 * @len: the expected size of data to be received in @buf
 *
 * Return: 0 on success or -errno on failure
 */
int i2c_read(int fd, unsigned char addr, unsigned char *buf, unsigned int len)
{
	struct i2c_msg msgs[1] = {
		{
			.addr = addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buf,
		},
	};

	struct i2c_rdwr_ioctl_data msgset = {
		.msgs = msgs,
		.nmsgs = 1,
	};

	int ret = ioctl(fd, I2C_RDWR, (unsigned long)&msgset);
	if (ret < 0) {
		log_error("I2C_RDWR ioctl failed: %s", strerror(errno));
		return -errno;
	}

	return 0;
}

/**
 * Writes data to an I2C device.
 *
 * @fd: I2C file descriptor
 * @addr: I2C slave address
 * @buf: buffer containing the data to be sent
 * @len: the size of data in @buf
 *
 * Return: 0 on success or -errno on failure
 */
int i2c_write(int fd, unsigned char addr, unsigned char *buf, unsigned int len)
{
	struct i2c_msg msgs[1] = {
		{
			.addr = addr,
			.flags = 0,
			.len = len,
			.buf = buf,
		},
	};

	struct i2c_rdwr_ioctl_data msgset = {
		.msgs = msgs,
		.nmsgs = 1,
	};

	int ret = ioctl(fd, I2C_RDWR, (unsigned long)&msgset);
	if (ret < 0) {
		log_error("I2C_RDWR ioctl failed: %s", strerror(errno));
		return -errno;
	}

	return 0;
}

/**
 * Performs a combined I2C write/read transaction.
 *
 * @fd: I2C file descriptor
 * @addr: I2C slave address
 * @buf_w: buffer containing the data to be sent
 * @len_w: the size of data in @buf_w
 * @buf_r: buffer to store received data
 * @len_r: the expected size of data to be received in @buf_r
 *
 * Return: 0 on success or -errno on failure
 */
int i2c_write_read(int fd, unsigned char addr,
				   unsigned char *buf_w, unsigned int len_w,
				   unsigned char *buf_r, unsigned int len_r)
{
	struct i2c_msg msgs[2] = {
		{
			.addr = addr,
			.flags = 0,
			.len = len_w,
			.buf = buf_w,
		},
		{
			.addr = addr,
			.flags = I2C_M_RD,
			.len = len_r,
			.buf = buf_r,
		},
	};

	struct i2c_rdwr_ioctl_data msgset = {
		.msgs = msgs,
		.nmsgs = 2,
	};

	int ret = ioctl(fd, I2C_RDWR, (unsigned long)&msgset);
	if (ret < 0) {
		log_error("I2C_RDWR ioctl failed: %s", strerror(errno));
		return -errno;
	}

	return 0;
}
