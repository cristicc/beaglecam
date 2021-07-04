/*
 * Common data structures and utilities used by both PRU0 and PRU1.
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _PRU_COMM_H
#define _PRU_COMM_H

/* Data captured from camera module, used in PRU0-to-PRU1 XFER */
struct cap_data {
	uint32_t seq;		/* Sequence number for error detection */
	uint8_t data[32];	/* 8 32-bit registers of data */
};

/*
 * Scratch Pad Bank Device IDs for PRU0-to-PRU1 XFER.
 * There are 3 banks, each having 30 32-bit registers (R29:0).
 */
#define SCRATCH_PAD_BANK_DEV0		10
#define SCRATCH_PAD_BANK_DEV1		11
#define SCRATCH_PAD_BANK_DEV2		12

/* The register no. from where the captured data starts to be stored on PRU0 */
#define XFER_DATA_START_REG_NO		8

/* PRU1-to-PRU0 interrupt as defined in the Linux DT */
#define PRU1_PRU0_INTERRUPT		17
/* TODO: DT needed? */
#define PRU0_PRU1_INTERRUPT		20

/*
 * PRU IO helpers
 */
#define HIGH				1
#define LOW				0

#define WRITE_PIN(bit, high)				\
	if (high)					\
		__R30 |= ((uint32_t) 1 << (bit));	\
	else						\
		__R30 &= ~((uint32_t) 1 << (bit))

#define READ_PIN(bit)			(__R31 >> (bit)) & 1

/* Diagnosis via LED blinking */
#define LED_DIAG_ENABLED

#ifndef LED_DIAG_ENABLED
#define BLINK_LED(pin, hz)		((void)0)
#else
/* Assuming PRU cores run at 200 MHz */
#define BLINK_LED(pin, hz)				\
	do {						\
		WRITE_PIN(pin, HIGH);			\
		__delay_cycles(100000000 / (hz));	\
		WRITE_PIN(pin, LOW);			\
		__delay_cycles(100000000 / (hz));	\
	} while(0)
#endif

#endif /* _PRU_COMM_H */
