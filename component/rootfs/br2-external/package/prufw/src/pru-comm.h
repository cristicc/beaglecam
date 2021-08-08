/*
 * Common data structures and utilities used by both PRU0 and PRU1.
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _PRU_COMM_H
#define _PRU_COMM_H

/* Track firmware changes */
#define PRU_FW_VERSION			"0.0.7"

/* Local address of the PRU shared RAM */
#define SHARED_MEM_ADDR			0x10000

/* PRU cores run at 200 MHz */
#define PRU_CYCLES_PER_USEC		200

/*
 * Inter-PRU signalling.
 */
struct pru_cmd {
	uint8_t id;			/* Member of enum pru_cmd_id */
	uint8_t arg;			/* Optional command argument */
};

enum pru_cmd_id {
	PRU_CMD_NONE = 0,		/* No command */
	PRU_CMD_ACK,			/* Common cmd acknoledge */
	PRU_CMD_CAP_START,		/* PRU0 to start frame aquisition */
	PRU_CMD_CAP_STOP,		/* PRU0 to stop frame aquisition */
};

/*
 * Layout of the 12 KB PRU shared RAM.
 * Currently used only for sending commands from PRU1 to PRU0.
 */
struct shared_mem {
	volatile struct pru_cmd pru0_cmd; /* Command sent from PRU1 to PRU0 */
	volatile struct pru_cmd pru1_cmd; /* Command sent from PRU0 to PRU1 */

	volatile struct {
		uint16_t xres;		/* Image X resolution */
		uint16_t yres;		/* Image Y resolution */
		uint8_t bpp;		/* Bits per pixel */
		uint32_t img_sz;	/* Image size in bytes */
		uint8_t test_mode;	/* Enable test image generation */
		uint8_t test_pclk_mhz;	/* Test image pixel clock freq (MHz) */
	} cap_config;
};

/* Helper to access PRU shared RAM */
#define SMEM	(*((volatile struct shared_mem *)SHARED_MEM_ADDR))

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
#define XFER_START_REG_NO		20

/* Stores data from src_buf into the specified scratch pad bank */
#define STORE_DATA(bank_no, src_buf)				\
	__xout(SCRATCH_PAD_BANK_DEV(bank_no), XFER_START_REG_NO, 0, src_buf)

/* Loads data from the specified scratch pad bank into dst_buf */
#define LOAD_DATA(bank_no, dst_buf)				\
	__xin(SCRATCH_PAD_BANK_DEV(bank_no), XFER_START_REG_NO, 0, dst_buf)

/* Sets bank_no to next scratch bank */
#define NEXT_BANK(bank_no)		(bank_no) = (bank_no) == 2 ? 0 : (bank_no) + 1

/*
 * PRU sleep helpers.
 */
#define NSLEEP(nsec)			__delay_cycles(PRU_CYCLES_PER_USEC * (nsec) / 1000)
#define USLEEP(usec)			__delay_cycles(PRU_CYCLES_PER_USEC * (usec))
#define MSLEEP(msec)			USLEEP(1000 * (msec))

/*
 * Alternative to __delay_cycles() intrinsic allowing for a variable
 * expression to be provided as argument.
 *
 * Note function input arguments are stored in R14..R29. For more details,
 * refer to "PRU Optimizing C/C+ Compiler User's Guide" document, section
 * "Function Structure and Calling Conventions".
 *
 * FIXME: For some reason, r14 doesn't hold the value of n.
 * As workaround, defined the function in delay-cycles-var.asm module.
 */
/*static inline void delay_cycles_var(uint32_t n)
{
	__asm(
		"	.newblock		\n"
		"$1:				\n"
		"	sub	r14, r14, 1	\n"
		"	qbne	$1, r14, 0	\n"
	);
}*/
extern void delay_cycles_var(uint32_t n);

/*
 * PRU IO helpers.
 */
#define HIGH				1
#define LOW				0

#define WRITE_PIN(bit, high)				\
	if (high)					\
		__R30 |= ((uint32_t)1 << (bit));	\
	else						\
		__R30 &= ~((uint32_t)1 << (bit))

#define READ_PIN(bit)			(__R31 >> (bit)) & 1

/*
 * LED blink frequency should be provided in dHz (= 0.1 Hz).
 */
#ifndef LED_DIAG_ENABLED
#define BLINK_LED(pin, dHz)		((void)0)
#else
#define BLINK_LED(pin, dHz)				\
	do {						\
		WRITE_PIN(pin, HIGH);			\
		MSLEEP(500 * 10 / (dHz));		\
		WRITE_PIN(pin, LOW);			\
		MSLEEP(500 * 10 / (dHz));		\
	} while(0)
#endif

#endif /* _PRU_COMM_H */
