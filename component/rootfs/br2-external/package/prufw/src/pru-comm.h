/*
 * Common data structures and utilities used by both PRU0 and PRU1.
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _PRU_COMM_H
#define _PRU_COMM_H

/* PRU0-to-PRU1 XFER */
typedef struct {
	uint32_t reg5;
	uint32_t reg6;
} bufferData;

/* PRU1-to-PRU0 interrupt as defined in the Linux DT */
#define PRU1_PRU0_INTERRUPT		17

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
