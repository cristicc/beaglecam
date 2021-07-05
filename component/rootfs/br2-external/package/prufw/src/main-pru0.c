/*
 * BeagleCam firmware for PRU0.
 *
 * Reads raw data from the camera module and stores it in a circular buffer
 * placed in the 12 KB shared memory.
 *
 * Although the camera supports 1 Mpixel frames, we are going to capture at the
 * lowest resolution QQVGA (160 x 120 pixels) for the moment. Each pixel is in
 * RGB 565 format, which means that 16 bits (2 bytes) are used per pixel. Hence,
 * each fame requires 38400 bytes, still much more than the available memory.
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
 * PRU0 starts waiting for a flag in the shared memory buffer to be set by PRU1
 * indicating that PRU0 should proceed reading data from the camera module. The
 * data will be written at the beginning of the circular buffer. While PRU1 is
 * reading the buffer, the start of the buffer is eventually overwritten with
 * the data corresponding to the end of the frame. To ensure a reliable data
 * transfer, PRU0 maintains a counter that is incremented for each frame line
 * that was captured from the camera. This information is written back to the
 * circular buffer, just before the line data, so that PRU1 receives packages
 * of size 321 bytes (1 byte hdr + 160 pixels/line * 2 bytes/pixel).
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

void main(void)
{
	struct cap_data buf;
	uint8_t capture_started = 0;
	uint8_t crt_bank;
	uint8_t iter;

	/* Clear the status of all interrupts */
	CT_INTC.SECR0 = 0xFFFFFFFF;
	CT_INTC.SECR1 = 0xFFFFFFFF;

	while (1) {
		/* Bit R31.30 is set when PRU1 triggered PRU1_PRU0_INTERRUPT */
		if (__R31 & HOST_INT) {
			/* Clear the status of the interrupt */
			CT_INTC.SICR_bit.STS_CLR_IDX = PRU1_PRU0_INTERRUPT;

			smem->pru1_cmd.command = PRU_CMD_ACK;
			if (smem->pru0_cmd.command == PRU_CMD_START_CAPTURE) {
				buf.seq = 0;
				crt_bank = 0;
				capture_started = 1;
			} else {
				capture_started = 0;
			}
		}

		if (capture_started == 0)
			continue;

		buf.seq++;
		for (iter = 0; iter < sizeof(buf.data); iter += 4) {
			*(uint32_t*)(buf.data + iter) = buf.seq + iter;
			__delay_cycles(200);
		}
		buf.data[0] = 0xee; buf.data[1] = 0xee; buf.data[2] = 0; buf.data[3] = buf.seq;
		buf.data[sizeof(buf.data) - 2] = 0xff; buf.data[sizeof(buf.data) - 1] = 0xff;

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
