/*
 * Utility to setup OV7670 camera module via the SCCB (I2C-like) interface.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "i2c-util.h"
#include "log.h"
#include "ov7670-i2c.h"
#include "ov7670-regs.h"

/*
 * Reads the value of a register.
 */
static int ov7670_read_reg(int i2c_fd, unsigned char i2c_addr,
						   unsigned char reg, unsigned char *value)
{
	//int ret = i2c_write_read(i2c_fd, i2c_addr, &reg, 1, value, 1);
	int ret = i2c_write(i2c_fd, i2c_addr, &reg, 1);
	if (ret != 0) {
		log_error("Failed to request ov7670 i2c reg 0x%02x", reg);
		return ret;
	}

	ret = i2c_read(i2c_fd, i2c_addr, value, 1);
	if (ret != 0)
		log_error("Failed to read ov7670 i2c reg 0x%02x", reg);

	return ret;
}

/*
 * Writes a list of register settings; 0xff / 0xff stops the process.
 */
static int ov7670_write_regs(int i2c_fd, const struct regval_list *regs)
{
	unsigned char buf[2];

	while (regs->reg_num != 0xff || regs->value != 0xff) {
		buf[0] = regs->reg_num;
		buf[1] = regs->value;

		if (write(i2c_fd, buf, 2) != 2) {
			log_error("failed to write ov7670 i2c reg 0x%02x: %s",
					  regs->reg_num, strerror(errno));
			return -errno;
		}

		if (regs->reg_num == REG_COM7 && (regs->value & COM7_RESET))
			usleep(5 * 1000);	/* Wait at least 1 ms for reset to complete */

		regs++;
	}

	return 0;
}

/*
 * Validates the chip manufacturer, product and version IDs.
 */
static int ov7670_detect(int i2c_fd, unsigned char i2c_addr)
{
	unsigned char v;
	int ret;

	ret = ov7670_read_reg(i2c_fd, i2c_addr, REG_MIDH, &v);
	if (ret != 0)
		return ret;

	if (v != 0x7f) {
		log_warn("Unexpected ov7670 MIDH: 0x%02x", v);
		return -ENODEV;
	}

	ret = ov7670_read_reg(i2c_fd, i2c_addr, REG_MIDL, &v);
	if (ret != 0)
		return ret;

	if (v != 0xa2) {
		log_warn("Unexpected ov7670 MIDL: 0x%02x", v);
		return -ENODEV;
	}

	ret = ov7670_read_reg(i2c_fd, i2c_addr, REG_PID, &v);
	if (ret < 0)
		return ret;

	if (v != 0x76) {
		log_warn("Unexpected ov7670 PID: 0x%02x", v);
		return -ENODEV;
	}

	ret = ov7670_read_reg(i2c_fd, i2c_addr, REG_VER, &v);
	if (ret < 0)
		return ret;

	if (v != 0x73) {
		log_warn("Unexpected ov7670 VER: 0x%02x", v);
		return -ENODEV;
	}

	log_info("Detected ov7670 i2c chip");
	return 0;
}

/**
 * Configures the OV7670 camera module using the I2C-compatible interface.
 *
 * @dev_path I2C camera device path
 *
 * Return: 0 on success or -errno on failure
 */
int ov7670_i2c_setup(const char *dev_path)
{
	const struct regval_list ov7670_custom_regs[] = {
		{ REG_CLKRC, 0x1 },					/* F(internal clock) = F(input clock)/2 */
		{ REG_COM7, COM7_FMT_QVGA | COM7_RGB },
		{ REG_COM10, COM10_PCLK_HB },		/* Suppress PCLK on horiz blank */
		{ REG_COM14, COM14_DCWEN | 0x1 },	/* DCW/PCLK-scale enable, PCLK divider=2 */
		//TODO: check if needed to set SCALING_PCLK_DIV[3:0] (0x73))
	};

	unsigned char cam_addr = OV7670_I2C_ADDR >> 1;
	int cam_fd, ret;

	cam_fd = i2c_open(dev_path, cam_addr);
	if (cam_fd < 0)
		return cam_fd;

	ret = ov7670_detect(cam_fd, cam_addr);
	if (ret != 0)
		goto err_close;

	ret = ov7670_write_regs(cam_fd, ov7670_get_regval_list(OV7670_REGS_DEFAULT));
	if (ret != 0)
		goto err_close;

	ret = ov7670_write_regs(cam_fd, ov7670_get_regval_list(OV7670_REGS_FMT_RGB565));
	if (ret != 0)
		goto err_close;

	ret = ov7670_write_regs(cam_fd, ov7670_custom_regs);
	if (ret != 0)
		goto err_close;

	return 0;

err_close:
	close(cam_fd);
	return ret;
}
