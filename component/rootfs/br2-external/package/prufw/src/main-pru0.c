/*
 * BeagleCam firmware for PRU0.
 *
 * Reads raw data from the camera module and transfered it to PRU1 via the 3
 * scratch pad banks.
 *
 * Although the camera supports 1 Mpixel frames, for the moment we are capturing
 * at the lowest resolution QQVGA (160 x 120 pixels). Each pixel is in RGB 565
 * format, which means that 16 bits (2 bytes) are used per pixel. Hence, each
 * frame requires 38400 bytes.
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

volatile register uint32_t __R30;
volatile register uint32_t __R31;

/* Shared memory used for inter-PRU communication */
volatile struct shared_mem *smem = (struct shared_mem *)SHARED_MEM_ADDR;

/* Host-0 interrupt sets bit 30 in register R31 */
#define HOST_INT			((uint32_t)1 << 30)

/*
 * Checks for commands from PRU1.
 * Returns the received command ID.
 */
uint8_t check_pru1_cmd() {
	uint8_t id;

	/* Bit R31.30 is set when PRU1 triggered PRU1_PRU0_INTERRUPT */
	if (__R31 & HOST_INT == 0)
		return PRU_CMD_NONE;

	/* Clear the status of the interrupt */
	CT_INTC.SICR_bit.STS_CLR_IDX = PRU1_PRU0_INTERRUPT;

	/* Get command ID and clear smem */
	id = smem->pru0_cmd.id;
	smem->pru0_cmd.id = PRU_CMD_NONE;

	/* Acknoledge command on PRU1 */
	smem->pru1_cmd.id = PRU_CMD_ACK;

	return id;
}

/*
 * Main loop.
 */
void main(void)
{
	struct cap_data buf;
	uint8_t capture_started = 0;
	uint8_t crt_bank;
	uint8_t iter;

	/* Clear the status of all interrupts */
	CT_INTC.SECR0 = 0xFFFFFFFF;
	CT_INTC.SECR1 = 0xFFFFFFFF;

	buf.pad = 0;

	while (1) {
		/* Process command from PRU1 */
		switch (check_pru1_cmd()) {
		case PRU_CMD_CAP_START:
			buf.seq = 0;
			crt_bank = 0;
			capture_started = 1;
			break;
		case PRU_CMD_CAP_STOP:
			capture_started = 0;
			break;
		}

		if (capture_started == 0)
			continue;

		/* Test only */
		buf.seq++;
		iter = 0;
		while (iter < sizeof(buf.data)) {
			if (buf.seq < 400) {
				/* RGB565 RED */
				buf.data[iter++] = 0xf8;
				buf.data[iter++] = 0x00;
			} else if (buf.seq < 800) {
				/* RGB565 GREEN */
				buf.data[iter++] = 0x07;
				buf.data[iter++] = 0xe0;
			} else {
				/* RGB565 BLUE */
				buf.data[iter++] = 0x00;
				buf.data[iter++] = 0x1f;
			}

			__delay_cycles(200);

			/* Process command from PRU1 */
			if (check_pru1_cmd() == PRU_CMD_CAP_STOP) {
				capture_started = 0;
				break;
			}
		}
		buf.len = iter;

		/*
		 * Store captured data in the current scratch pad bank. Note the
		 * switch is necessary for "error #664: expected an integer constant".
		 */
		switch (crt_bank) {
		case 0:
			STORE_DATA(0, buf);
			break;
		case 1:
			STORE_DATA(1, buf);
			break;
		case 2:
			STORE_DATA(2, buf);
			break;
		}

		/*
		 * Trigger interrupt on PRU1, see ARM335x TRM section:
		 * Event Interface Mapping (R31): PRU System Events
		 */
		__R31 = PRU0_PRU1_INTERRUPT + 16;

		/* Move to next scratch bank */
		NEXT_BANK(crt_bank);
	}
}
