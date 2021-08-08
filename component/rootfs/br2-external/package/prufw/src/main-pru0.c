/*
 * BeagleCam firmware for PRU0.
 *
 * Reads raw data from the camera module and transfered it to PRU1 via the 3
 * scratch pad banks. The data is in RGB565 format, which means that 16 bits
 * (2 bytes) are used per pixel.
 *
 * The start of each frame is signalled by VSYNC going low while the first line
 * of data appears when HREF goes high. Since from PRU0 we can only access data
 * lines D0-D7, PCLK and HREF signals, the VSYNC signal is handled by PRU1.
 *
 * Once the camera has been setup via the I2C-like interface for QQVGA mode,
 * PCLK runs at 2 MHz, therefore the clock period is 0.5 usec. After HREF goes
 * high, the line data is read in ~160 usec (160 pixels * 2 B/pixel x 0.5 usec),
 * followed by 640 usec delay until the next line (see "QQVGA Frame Timing" in
 * OV7670 datasheet). After the last line in a frame (i.e. 120th line) there is
 * ~900 usec delay until VSYNC goes high to indicate the frame is complete.
 *
 * PRU0 starts waiting for a command in the shared memory buffer to be set by
 * PRU1 indicating that PRU0 should proceed reading data from the camera module.
 * To ensure a reliable data transfer, PRU0 maintains a sequence counter that
 * is incremented before each data transfer.
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <stdint.h>
#include <pru_intc.h>

#include "pru-comm.h"
#include "resource_table_0.h"

#define PRU0

volatile register uint32_t __R31;

/* Global data */
static struct cap_data frm_data;
static uint8_t capture_started;
static uint8_t crt_bank;
static uint16_t test_pclk_cycles;

/*
 * Checks for commands from PRU1.
 * Returns the received command ID.
 */
static uint8_t check_pru1_cmd() {
	static uint8_t id;

	/* Check SMEM for new command from PRU1 */
	id = SMEM.pru0_cmd.id;
	if (id == PRU_CMD_NONE)
		return id;

	/* Clear SMEM */
	SMEM.pru0_cmd.id = PRU_CMD_NONE;

	switch (id) {
	case PRU_CMD_CAP_START:
		capture_started = 1;
		crt_bank = 0;

		/* Invalidate content of scratch pad banks */
		frm_data.seq = 0;
		frm_data.len = 0;
		STORE_DATA(0, frm_data);
		STORE_DATA(1, frm_data);
		STORE_DATA(2, frm_data);

		if (SMEM.cap_config.test_mode != 0)
			test_pclk_cycles = sizeof(frm_data.data) *
					   PRU_CYCLES_PER_USEC / SMEM.cap_config.test_pclk_mhz;
		break;

	case PRU_CMD_CAP_STOP:
		capture_started = 0;
		break;

	default:
		id = PRU_CMD_NONE;
	}

	/* Send ACK command to PRU1 */
	SMEM.pru1_cmd.id = PRU_CMD_ACK;

	return id;
}

/*
 * Generates test RGB565 pixels stored in BGR (little endian) format.
 */
static void generate_test_data(struct cap_data *buf)
{
	uint32_t img_part_size = SMEM.cap_config.img_sz / 3;
	uint32_t img_part_off = buf->seq * sizeof(buf->data);
	uint8_t iter = 0;

	while (iter < sizeof(buf->data)) {
		if (img_part_off < img_part_size) {
			/* RED */
			buf->data[iter++] = 0x00;
			buf->data[iter++] = 0xf8;
		} else if (img_part_off < 2 * img_part_size) {
			/* GREEN */
			buf->data[iter++] = 0xe0;
			buf->data[iter++] = 0x07;
		} else {
			/* BLUE */
			buf->data[iter++] = 0x1f;
			buf->data[iter++] = 0x00;
		}

		img_part_off += 2;
	}

	buf->len = iter;
}

/*
 * Main loop.
 */
void main(void)
{
	/* Clear the status of all interrupts */
	CT_INTC.SECR0 = 0xFFFFFFFF;
	CT_INTC.SECR1 = 0xFFFFFFFF;

	/* Init data */
	frm_data.pad = 0;
	capture_started = 0;

	while (1) {
		/* Process command from PRU1 */
		check_pru1_cmd();

check_status:
		if (capture_started == 0)
			continue;

		if (SMEM.cap_config.test_mode != 0) {
			generate_test_data(&frm_data);

			/* Simulate PCLK */
			delay_cycles_var(test_pclk_cycles);

			if (check_pru1_cmd() != PRU_CMD_NONE)
				goto check_status;

		} else {
			//TODO: get data from camera module
		}

		frm_data.seq++;

		/*
		 * Store captured data in the current scratch pad bank.
		 * Note using the switch statement as workaround for:
		 * "error #664: expected an integer constant"
		 */
		switch (crt_bank) {
		case 0:
			STORE_DATA(0, frm_data);
			break;
		case 1:
			STORE_DATA(1, frm_data);
			break;
		case 2:
			STORE_DATA(2, frm_data);
			break;
		}

		/* Move to next scratch bank */
		NEXT_BANK(crt_bank);
	}
}
