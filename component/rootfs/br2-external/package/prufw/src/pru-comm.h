/*
 * Common data structures and utilities used by both PRU0 and PRU1.
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _PRU_COMM_H
#define _PRU_COMM_H

/*
 * Inter-PRU command structure.
 */
struct pru_cmd {
	uint8_t command;
	uint8_t arg;			/* Optional command argument */
};

/*
 * Shared commands IDs.
 */
#define PRU_CMD_NONE			0
#define PRU_CMD_ACK			1

/*
 * PRU1 -> PRU0 commands IDs.
 */
#define PRU_CMD_START_CAPTURE		2
#define PRU_CMD_STOP_CAPTURE		3

/*
 * Layout of the 12 KB PRU shared RAM.
 * Currently used only for sending commands from PRU1 to PRU0.
 */
struct shared_mem {
	struct pru_cmd pru0_cmd;	/* Command sent from PRU1 to PRU0 */
	struct pru_cmd pru1_cmd;	/* Command sent from PRU0 to PRU1 */
	uint8_t data[0];		/* Reserved */
};

/* Local address of the PRU shared RAM */
#define SHARED_MEM_ADDR			0x10000

/*
 * Data captured from camera module by PRU0, XFER-ed to PRU1.
 */
struct cap_data {
	uint32_t seq;			/* Sequence no. for error detection */
	uint8_t data[32];		/* 8 x 32-bit registers of data */
};

/*
 * Converts scratch pad bank zero-based indexes to device IDs (10 - 12).
 * There are 3 banks, each having 30 32-bit registers (R29:0).
 */
#define SCRATCH_PAD_BANK_DEV(bank_no)	(10 + (bank_no))

/* The register no. from where data XFER should start */
#define XFER_DATA_START_REG_NO		8

/* Stores data from src_buf into the specified scratch pad bank */
#define STORE_DATA(bank_no, src_buf)				\
	__xout(SCRATCH_PAD_BANK_DEV(bank_no), XFER_DATA_START_REG_NO, 0, src_buf)

/* Loads data from the specified scratch pad bank into dst_buf */
#define LOAD_DATA(bank_no, dst_buf)				\
	__xin(SCRATCH_PAD_BANK_DEV(bank_no), XFER_DATA_START_REG_NO, 0, dst_buf)

/* Sets bank_no to next scratch bank */
#define NEXT_BANK(bank_no)		(bank_no) = (bank_no == 2) ? 0 : (bank_no + 1)

/* PRU1-to-PRU0 irq (shared unused RPMsg irq defined as 'kick' in Linux DT) */
#define PRU1_PRU0_INTERRUPT		17
/* PRU0-to-PRU1 irq (TODO: define 'xfer' in the Linux DT) */
#define PRU0_PRU1_INTERRUPT		20

/*
 * PRU IO helpers
 */
#define HIGH				1
#define LOW				0

#define WRITE_PIN(bit, high)				\
	if (high)					\
		__R30 |= ((uint32_t)1 << (bit));	\
	else						\
		__R30 &= ~((uint32_t)1 << (bit))

#define READ_PIN(bit)			(__R31 >> (bit)) & 1

/* Diagnosis via LED blinking */
#define LED_DIAG_ENABLED

/* PRU cores run at 200 MHz */
#ifndef LED_DIAG_ENABLED
#define BLINK_LED(pin, hz)		((void)0)
#else
#define BLINK_LED(pin, hz)				\
	do {						\
		WRITE_PIN(pin, HIGH);			\
		__delay_cycles(100000000 / (hz));	\
		WRITE_PIN(pin, LOW);			\
		__delay_cycles(100000000 / (hz));	\
	} while(0)
#endif

#endif /* _PRU_COMM_H */
