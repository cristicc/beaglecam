/*
 * Common data structures and utilities used by both PRU0 and PRU1.
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _PRU_COMM_H
#define _PRU_COMM_H

#define PRU_FW_VERSION			"0.0.2"

/*
 * Inter-PRU signalling.
 */
enum pru_cmd_id {
	PRU_CMD_NONE = 0,		/* No command */
	PRU_CMD_ACK,			/* Common cmd acknoledge */
	PRU_CMD_CAP_START,		/* PRU0 to start frame aquisition */
	PRU_CMD_CAP_STOP,		/* PRU0 to stop frame aquisition */
};

struct pru_cmd {
	uint8_t id;			/* Member of enum pru_cmd_id */
	uint8_t arg;			/* Optional command argument */
};

/*
 * Layout of the 12 KB PRU shared RAM.
 * Currently used only for sending commands from PRU1 to PRU0.
 */
struct shared_mem {
	struct pru_cmd pru0_cmd;	/* Command sent from PRU1 to PRU0 */
	struct pru_cmd pru1_cmd;	/* Command sent from PRU0 to PRU1 */
	uint8_t data[0];		/* Currently not used */
};

/* Local address of the PRU shared RAM */
#define SHARED_MEM_ADDR			0x10000

/*
 * Data captured from camera module by PRU0 and XFER-ed to PRU1.
 */
struct cap_data {
	uint16_t seq;			/* Sequence no. for error detection */
	uint8_t len;			/* Data size */
	uint8_t pad;			/* Padding */
	uint8_t data[32];		/* Raw image data */
};

/*
 * Converts scratch pad bank zero-based indexes to device IDs (10 - 12).
 * There are 3 banks, each having 30 x 32-bit registers (R29:0), but only
 * 44 bytes can be transferred during an __xin() or __xout() operation.
 */
#define SCRATCH_PAD_BANK_DEV(bank_no)	(10 + (bank_no))

/* The register no. from where data XFER should start */
#define XFER_START_REG_NO		8

/* Stores data from src_buf into the specified scratch pad bank */
#define STORE_DATA(bank_no, src_buf)				\
	__xout(SCRATCH_PAD_BANK_DEV(bank_no), XFER_START_REG_NO, 0, src_buf)

/* Loads data from the specified scratch pad bank into dst_buf */
#define LOAD_DATA(bank_no, dst_buf)				\
	__xin(SCRATCH_PAD_BANK_DEV(bank_no), XFER_START_REG_NO, 0, dst_buf)

/* Sets bank_no to next scratch bank */
#define NEXT_BANK(bank_no)		(bank_no) = (bank_no) == 2 ? 0 : (bank_no) + 1

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

/*
 * LED blink frequency should be provided in dHz (1/10 Hz).
 * Note the PRU cores frequency is 200 MHz.
 */
#ifndef LED_DIAG_ENABLED
#define BLINK_LED(pin, dHz)		((void)0)
#else
#define BLINK_LED(pin, dHz)				\
	do {						\
		WRITE_PIN(pin, HIGH);			\
		__delay_cycles(10 * 100000000 / (dHz));	\
		WRITE_PIN(pin, LOW);			\
		__delay_cycles(10 * 100000000 / (dHz));	\
	} while(0)
#endif

#endif /* _PRU_COMM_H */
