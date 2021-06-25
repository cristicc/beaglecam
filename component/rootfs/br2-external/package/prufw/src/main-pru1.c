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
#define LED_DIAG_ENABLED
#define PIN_LED				13 /* P8_20 */

/*
 * State machine.
 */
#define SM_STOPPED			0
#define SM_STARTED				2

/*
 * Command sent from ARM to PRU.
 */
#define BCAM_CMD_MAGIC			0xbeca
#define BCAM_CMD_START			1
#define BCAM_CMD_STOP			2
#define BCAM_CMD_ACK			3

struct bcam_cmd {
	uint16_t magic;			/* Filter out garbage/corrupted data. */
	uint16_t command;
};

/* Used to implement pru_rpmsg_send_optim() */
#define RPMSG_MESSAGE_OPTIMIZED_SIZE	480

/*
 * Global variables.
 */
uint16_t rpmsg_send_optim_counter = 0;

uint8_t run_state = SM_STOPPED;
uint8_t cur_seq_num = 0;
uint8_t arm_recv_buf[RPMSG_MESSAGE_SIZE];

bufferData pru0_recv_buf;

/*
 * Initialize PRU core.
 */
void init_pru_core()
{
	/*
	 * Allow OCP master port access by the PRU, so the PRU can read
	 * external memories.
	 */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

	/* 3 Hz LED blink */
	BLINK_LED(PIN_LED, 3);
	BLINK_LED(PIN_LED, 3);
	BLINK_LED(PIN_LED, 3);
}

/*
 * Initialize RPMsg subsystem.
 */
void init_rpmsg(struct pru_rpmsg_transport *transport)
{
	volatile uint8_t *status;

	/*
	 * Clear the status of the PRU-ICSS system event that the ARM
	 * will use to 'kick' us.
	 */
	CT_INTC.SICR_bit.STS_CLR_IDX = FROM_ARM_HOST;

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
	struct pru_virtqueue		*virtqueue;

	if (rpmsg_send_optim_counter == 0) {
		virtqueue = &transport->virtqueue0;

		/* Get an available buffer */
		head = pru_virtqueue_get_avail_buf(virtqueue, (void **)&msg, &msg_len);

		if (head < 0)
			return PRU_RPMSG_NO_BUF_AVAILABLE;
	}

	/* Copy local data buffer to the descriptor buffer address */
	memcpy(msg->data + rpmsg_send_optim_counter, data, len);
	rpmsg_send_optim_counter += len;

	if (rpmsg_send_optim_counter < RPMSG_MESSAGE_OPTIMIZED_SIZE) {
		return PRU_RPMSG_NO_KICK;
	}

	msg->len = rpmsg_send_optim_counter;
	msg->dst = dst;
	msg->src = src;
	msg->flags = 0;
	msg->reserved = 0;

	/* Add the used buffer */
	if (pru_virtqueue_add_used_buf(virtqueue, head, msg_len) < 0)
		return PRU_RPMSG_INVALID_HEAD;

	/* Kick the ARM host */
	pru_virtqueue_kick(virtqueue);

	rpmsg_send_optim_counter = 0;

	return PRU_RPMSG_SUCCESS;
}

void pru_start() {
	run_state = SM_STARTED;
	WRITE_PIN(PIN_LED, HIGH);

	/*
	 * Trigger interrupt on PRU0, see ARM335x TRM section:
	 * Event Interface Mapping (R31): PRU System Events
	 */
	__R31 = PRU1_PRU0_INTERRUPT + 16;
}

void pru_stop() {
	run_state = SM_STOPPED;
	WRITE_PIN(PIN_LED, LOW);

	/* Tell PRU0 to stop */
	//TODO: stop PRU0
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

	pru_stop();

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

	init_pru_core();
	init_rpmsg(&transport);

	while (1) {
		/* Check bit 31 of R31 reg to see if the ARM has kicked us */
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
					pru_start();
					send_rpmsg(&transport, rpmsg_dst, rpmsg_src,
						   "pru started");

					break;

				case BCAM_CMD_STOP:
					pru_stop();
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

		switch (run_state) {
		case SM_STARTED:
			/*
			 * XFR registers R5-R10 from PRU0 to PRU1
			 * 14 is the device_id reserved for PRU-to-PRU transfer.
			 */
			__xin(14, 5, 0, pru0_recv_buf);

			//TODO: detect timeout - use a seq. no.

			/*pru_rpmsg_send_optim(&transport, rpmsg_dst, rpmsg_src,
					     (void*)(&pru0_recv_buf), sizeof(pru0_recv_buf));*/
			pru_rpmsg_send(&transport, rpmsg_dst, rpmsg_src,
				       (void*)(&pru0_recv_buf), sizeof(pru0_recv_buf));

			break;
		}
	}
}
