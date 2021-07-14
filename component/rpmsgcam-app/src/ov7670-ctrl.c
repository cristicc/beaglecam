/*
 * Setup OV7670 camera module via the SCCB interface compatible with I2C.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <stdio.h>

#include "i2cfunc.h"
#include "ov7670-ctrl.h"
#include "ov7670-regs.h"

/* I2C handler */
static const char *cam_dev;
static unsigned char cam_addr;
static int cam_fd;

//TODO: get rid of printf and use logging

static unsigned char cam_reg_read(unsigned char reg)
{
	int ret;
	unsigned char buf[2];

	buf[0] = reg;

	/* Despite being an I2C clone, SCCB does not have NACK or ACK */
	i2c_write_ignore_nack(cam_fd, cam_addr, buf, 1);

	/* TODO: do we really need closing and reopening fd? */
	i2c_close(cam_fd);
	cam_fd = i2c_open(cam_dev, cam_addr);

	ret = i2c_read_no_ack(cam_fd, cam_addr, buf, 1);

    if (ret < 0) {
		printf("Failed to read camera reg: %d", ret);
		buf[0] = 0;
	}

	return buf[0];
}

static void cam_id_dump(void)
{
	unsigned char val;
	val = cam_reg_read(REG_MIDH);
	printf("MIDH: 0x%02x\n", val);

	val = cam_reg_read(REG_MIDL);
	printf("MIDL: 0x%02x\n", val);

	val = cam_reg_read(REG_VER);
	printf("VER: 0x%02x\n", val);

	val = cam_reg_read(REG_PID);
	printf("PID: 0x%02x\n", val);
}

int cam_init(const char *i2c_dev)
{
	/*int i = 0;*/
	unsigned char pair[2];

	cam_dev = i2c_dev;
	cam_addr = (OV7670_I2C_ADDR >> 1);

	cam_fd = i2c_open(cam_dev, cam_addr);
	if (cam_fd < 0)
		return cam_fd;

	/* Do a reset */
	pair[0] = 0x12; pair[1] = 0x80;
	i2c_write_ignore_nack(cam_fd, cam_addr, pair, 2);
	delay_ms(999);

	/*do {
		pair[0] = params_qvga[i];
		pair[1] = params_qvga[i+1];

		i2c_write_ignore_nack(cam_fd, cam_addr, pair, 2);
		delay_ms(10);
		i += 2;
	} while (not_finished);*/

	pair[0] = REG_COM7; pair[1] = 0x63;
	i2c_write_ignore_nack(cam_fd, cam_addr, pair, 2);
	delay_ms(10);

	pair[0] = REG_COM15; pair[1] = COM15_RGB565;
	i2c_write_ignore_nack(cam_fd, cam_addr, pair, 2);
	delay_ms(10);

	cam_id_dump();
	i2c_close(cam_fd);

	return 0;
}
