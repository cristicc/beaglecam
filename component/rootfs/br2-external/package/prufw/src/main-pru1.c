/*
 * BeagleCam firmware for PRU1.
 *
 * Writes the data captured by PRU0 to the host via the RPMSG infrastructure.
 *
 * Once PRU1 is initialized, it starts monitoring the VSYNC signal to detect
 * the start of a new frame and it notifies PRU0 to read the raw data from the
 * camera module and store it in the circular buffer located in shared memory.
 * The data is currently packaged per frame line and each package contains a
 * header with the line number which is validated by PRU1 and removed prior to
 * sending it further to the host.
 *
 * Note the maximum RPMSG message size is 512 bytes, but only 496 bytes can be
 * used for actual data since 16 bytes are reserved for the message header (see
 * RPMSG_BUF_SIZE, RPMSG_MESSAGE_SIZE, RPMSG_HEADER_SIZE in pru_rpmsg.h and
 * struct pru_rpmsg_hdr in pru_rpmsg.c).

 * To allow validation of the incoming data on the host side, PRU1 uses a
 * one-byte sequence number followed by 480 bytes of pixel data. For QQVGA
 * frame resolution, this means 1.5 lines, while the sequence number ranges
 * from 0 to 79 (120 lines / 1.5 lines/pkg). A reserved value of 0xFF is used
 * to indicate an invalid transfer on PRU0 side.
 *
 * The host can enable/disable frame aquisition by sending a special RPMSG
 * package to PRU1. Additionally, frame aquisition will be automatically
 * disabled if there is a failure to send a message to the host (e.g. host
 * buffer full).
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <stdint.h>
#include <string.h>
#include <pru_cfg.h>
#include <pru_intc.h>
#include <pru_rpmsg.h>

#include "pru-comm.h"
#include "resource_table_1.h"

#define PRU1

volatile register uint32_t __R30;
volatile register uint32_t __R31;

/* Host-1 Interrupt sets bit 31 in register R31 */
#define HOST_INT			((uint32_t) 1 << 31)

/* PRU-ICSS system events used for RPMsg as defined in the Linux DT. */
#define TO_ARM_HOST			18
#define FROM_ARM_HOST			19

/* PRU channel as defined in rpmsgcam Linux kernel driver. */
#define CHAN_NAME			"rpmsg-cam"
#define CHAN_DESC			"Channel 31" /* Not actually used */
#define CHAN_PORT			31

/*
 * Used to ensure the rpmsgcam driver is ready for RPMsg communication.
 * Found at linux-x.y.z/include/uapi/linux/virtio_config.h
 */
#define VIRTIO_CONFIG_S_DRIVER_OK	4

/* Diagnosis via LED blinking */
#define PIN_LED				13 /* P8_20 */

/*
 * State machine.
 */
#define SM_STOPPED			0
#define SM_STARTED			1

/*
 * Command sent from ARM to PRU.
 */
#define BCAM_CMD_MAGIC			0xbeca
#define BCAM_CMD_START			1
#define BCAM_CMD_STOP			2

struct bcam_cmd {
	uint16_t magic;			/* Filter out garbage/corrupted data. */
	uint16_t command;
};

/* Used to implement pru_rpmsg_send_optim() */
#define RPMSG_MESSAGE_OPTIMIZED_SIZE	464

/*
 * Global variables.
 */
uint8_t run_state = SM_STOPPED;
uint8_t cur_seq_num = 0;
uint8_t arm_recv_buf[RPMSG_MESSAGE_SIZE];

/*
 * Initialize PRU core.
 */
void init_pru_core()
{
	uint8_t blinks;

	/*
	 * Allow OCP master port access by the PRU, so the PRU can read
	 * external memories.
	 */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

	/*
	 * Writing an index number to the EN_SET_IDX section of the EISR
	 * register results in enabling of that interrupt.
	 */
	CT_INTC.EISR_bit.EN_SET_IDX = PRU0_PRU1_INTERRUPT;

	/* Clear the status of all interrupts */
	CT_INTC.SECR0 = 0xFFFFFFFF;
	CT_INTC.SECR1 = 0xFFFFFFFF;

	/* 3 Hz LED blink for 2 seconds */
	for (blinks = 0; blinks < 6; blinks++) {
		BLINK_LED(PIN_LED, 3);
	}
}

/*
 * Initialize RPMsg subsystem.
 */
void init_rpmsg(struct pru_rpmsg_transport *transport)
{
	volatile uint8_t *status;

	/* Wait for the rpmsgcam driver to be ready for RPMsg communication */
	status = &resourceTable.rpmsg_vdev.status;
	while (!(*status & VIRTIO_CONFIG_S_DRIVER_OK)) {
		BLINK_LED(PIN_LED, 2);
	}

	/* Initialize the RPMsg transport structure */
	pru_rpmsg_init(transport, &resourceTable.rpmsg_vring0,
		&resourceTable.rpmsg_vring1, TO_ARM_HOST, FROM_ARM_HOST);

	/*
	 * Create the PRMsg channel between the PRU and ARM user space using
	 * the given transport structure.
	 */
	while (pru_rpmsg_channel(RPMSG_NS_CREATE, transport, CHAN_NAME,
			CHAN_DESC, CHAN_PORT) != PRU_RPMSG_SUCCESS) {
		BLINK_LED(PIN_LED, 1);
	}
}

/*
 * Copied from pru_rpmsg.c as required by pru_rpmsg_send_optim() below.
 */
struct pru_rpmsg_hdr {
	uint32_t	src;
	uint32_t	dst;
	uint32_t	reserved;
	uint16_t	len;
	uint16_t	flags;
	uint8_t		data[0];
};

/*
 * Re-implementation of pru_rpmsg_send() to optimize data transfer by caching
 * the data blocks until the total size of the data reaches or exceeds
 * RPMSG_MESSAGE_OPTIMIZED_SIZE bytes. When that happens, the ARM host is
 * kicked to process the current buffer and a new one will be used for
 * subsequent package(s), restarting the process.
 *
 * To force the data transfer, call the function with 'len' set to 0.
 */
int16_t pru_rpmsg_send_optim(
	struct pru_rpmsg_transport	*transport,
	uint32_t			src,
	uint32_t			dst,
	void				*data,
	uint16_t			len
)
{
	static struct pru_rpmsg_hdr	*msg;
	static uint32_t			msg_len;
	static int16_t			head;
	static struct pru_virtqueue	*virtqueue;
	static uint16_t 		cached_len = 0;

	if (len != 0) {
		if (cached_len == 0) {
			virtqueue = &transport->virtqueue0;

			/* Get an available buffer */
			head = pru_virtqueue_get_avail_buf(virtqueue, (void **)&msg, &msg_len);

			if (head < 0)
				return PRU_RPMSG_NO_BUF_AVAILABLE;
		}

		/* Copy local data buffer to the descriptor buffer address */
		memcpy(msg->data + cached_len, data, len);
		cached_len += len;

		if (cached_len < RPMSG_MESSAGE_OPTIMIZED_SIZE) {
			return PRU_RPMSG_NO_KICK;
		}
	} else if (cached_len == 0) {
		return PRU_RPMSG_NO_KICK;
	}

	msg->len = cached_len;
	msg->dst = dst;
	msg->src = src;
	msg->flags = 0;
	msg->reserved = 0;

	/* Add the used buffer */
	if (pru_virtqueue_add_used_buf(virtqueue, head, msg_len) < 0)
		return PRU_RPMSG_INVALID_HEAD;

	/* Kick the ARM host */
	pru_virtqueue_kick(virtqueue);

	cached_len = 0;

	return PRU_RPMSG_SUCCESS;
}

void start_stop_pru(uint8_t start) {
	if (start) {
		run_state = SM_STARTED;
		WRITE_PIN(PIN_LED, HIGH);
	} else {
		run_state = SM_STOPPED;
		WRITE_PIN(PIN_LED, LOW);
	}

	/*
	 * Trigger interrupt on PRU0, see ARM335x TRM section:
	 * Event Interface Mapping (R31): PRU System Events
	 */
	__R31 = PRU1_PRU0_INTERRUPT + 16;
}

/*
 * Send message to the host system using the address from which the last
 * host command has been received.
 */
int send_rpmsg(struct pru_rpmsg_transport *transport,
	       uint32_t src, uint32_t dst, const char *msg)
{
	if (pru_rpmsg_send(transport, src, dst, (void*)msg, strlen(msg)) == PRU_RPMSG_SUCCESS)
		return 0;

	start_stop_pru(0);

	/* Attempt to destory the existing channe.l */
	pru_rpmsg_channel(RPMSG_NS_DESTROY, transport,
			  CHAN_NAME, CHAN_DESC, CHAN_PORT);

	/* Attempt to reinitialize the RPmsg facility */
	init_rpmsg(transport);

	return 1;
}

/*
 * Main loop.
 */
void main(void)
{
	struct pru_rpmsg_transport transport;
	uint16_t rpmsg_src, rpmsg_dst, len;
	struct bcam_cmd *cmd = (struct bcam_cmd *) arm_recv_buf;

	struct cap_data pru0_recv_buf;
	uint32_t expect_seq;
	uint8_t crt_bank;

	init_pru_core();
	init_rpmsg(&transport);

	while (1) {
		/* Bit R31.31 is set when the ARM has kicked us */
		if (__R31 & HOST_INT) {
			/* Clear the event status */
			CT_INTC.SICR_bit.STS_CLR_IDX = FROM_ARM_HOST;

			/*
			 * Receive all available messages, multiple messages
			 * can be sent per kick.
			 */
			while (pru_rpmsg_receive(&transport, &rpmsg_src, &rpmsg_dst,
					arm_recv_buf, &len) == PRU_RPMSG_SUCCESS)
			{
				if (len < sizeof(*cmd) || cmd->magic != BCAM_CMD_MAGIC)
					continue;

				switch (cmd->command) {
				case BCAM_CMD_START:
					start_stop_pru(1);

					expect_seq = 1;
					crt_bank = SCRATCH_PAD_BANK_DEV0;

					send_rpmsg(&transport, rpmsg_dst, rpmsg_src,
						   "pru started");
					break;

				case BCAM_CMD_STOP:
					start_stop_pru(0);

					send_rpmsg(&transport, rpmsg_dst, rpmsg_src,
						   "pru stopped");
					break;

				default:
					send_rpmsg(&transport, rpmsg_dst, rpmsg_src,
						   "invalid cmd");
					break;
				}
			}
		}

		if (run_state != SM_STARTED)
			continue;

		/*
		 * Check PRU0_PRU1_INTERRUPT generated by PRU0 when captured
		 * data is ready to be read by PRU1 from one of the banks.
		 */
		if ((CT_INTC.SECR0_bit.ENA_STS_31_0 & (1 << PRU0_PRU1_INTERRUPT)) == 0)
			continue;

		/* Clear the status of PRU0_PRU1_INTERRUPT */
		CT_INTC.SICR_bit.STS_CLR_IDX = PRU0_PRU1_INTERRUPT;

		/*
		 * Using switch is a workaround for the __xin() related compiler error:
		 * error #664: expected an integer constant
		 */
		switch (crt_bank) {
		case SCRATCH_PAD_BANK_DEV0:
			__xin(SCRATCH_PAD_BANK_DEV0, XFER_DATA_START_REG_NO,
			      0, pru0_recv_buf);
			break;
		case SCRATCH_PAD_BANK_DEV1:
			__xin(SCRATCH_PAD_BANK_DEV1, XFER_DATA_START_REG_NO,
			      0, pru0_recv_buf);
			break;
		case SCRATCH_PAD_BANK_DEV2:
			__xin(SCRATCH_PAD_BANK_DEV2, XFER_DATA_START_REG_NO,
			      0, pru0_recv_buf);
			break;
		}

		/* Move to next scratch bank */
		crt_bank = (crt_bank == SCRATCH_PAD_BANK_DEV2)
				? SCRATCH_PAD_BANK_DEV0 : crt_bank + 1;

		pru_rpmsg_send_optim(&transport, rpmsg_dst, rpmsg_src,
				     (void*)(&pru0_recv_buf.data), sizeof(pru0_recv_buf.data));
		/*pru_rpmsg_send(&transport, rpmsg_dst, rpmsg_src,
			       (void*)(&pru0_recv_buf), sizeof(pru0_recv_buf));*/

		if (pru0_recv_buf.seq > expect_seq) {
			start_stop_pru(0);

			/* Send out any cached data */
			pru_rpmsg_send_optim(&transport, rpmsg_dst, rpmsg_src, 0, 0);

			send_rpmsg(&transport, rpmsg_dst, rpmsg_src,
				   "recv unexpected seq");
			break;
		}

		if (++expect_seq > 30) {
			start_stop_pru(0);
			/* Send out any cached data */
			pru_rpmsg_send_optim(&transport, rpmsg_dst, rpmsg_src, 0, 0);
		}
	}
}
