/*
 * BeagleCam firmware for PRU1.
 *
 * Writes the data captured by PRU0 to ARM host via the RPMsg infrastructure.
 *
 * Once PRU1 is initialized, it starts monitoring the VSYNC signal to detect
 * the start of a new frame and it notifies PRU0 to read the raw data from the
 * camera module and transfer it to PRU1 via the scratch pad banks.
 *
 * Note the maximum RPMSG message size is 512 bytes, but only 496 bytes can be
 * used for actual data since 16 bytes are reserved for the message header (see
 * RPMSG_BUF_SIZE, RPMSG_MESSAGE_SIZE, RPMSG_HEADER_SIZE in pru_rpmsg.h and
 * struct pru_rpmsg_hdr in pru_rpmsg.c).

 * To allow validation of the incoming data on the ARM  side, PRU1 adds a 1-byte
 * frame section ID and a 2-byte sequence number, followed by pixel data. The
 * sequence number is reset when the frame section changes.
 *
 * The host can manage the frame aquisition by sending dedicated RPMsg commands
 * to PRU1. Additionally, frame aquisition is automatically stopped in case
 * unexpected errors occured. Those errors are sent to the host via dedicated
 * log messages.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <stdint.h>

#include <pru_cfg.h>
#include <pru_ctrl.h>
#include <pru_intc.h>
#include <pru_rpmsg.h>

#include "bcam-rpmsg-api.h"
#include "pru-comm.h"
#include "resource_table_1.h"

#define PRU1

volatile register uint32_t __R30;
volatile register uint32_t __R31;

/* Host-1 Interrupt sets bit 31 in register R31 */
#define HOST_INT			((uint32_t)1 << 31)

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

/* Timeout waiting for ACKs from PRU0 */
#define PRU0_ACK_TMOUT_USEC		1000

/* Timeout waiting for cap_data messages from PRU0 */
#define PRU0_CAP_TMOUT_USEC		5000

/* Diagnosis via LED blinking */
#define PIN_LED				13 /* P8_20 */

/*
 * Standard structure copied from pru_rpmsg.c.
 * Currently used to implement rpmsg_send_cap().
 */
struct pru_rpmsg_hdr {
	uint32_t src;
	uint32_t dst;
	uint32_t reserved;
	uint16_t len;
	uint16_t flags;
	uint8_t data[0];
};

/*
 * Global variables.
 */
static uint8_t arm_recv_buf[RPMSG_MESSAGE_SIZE];
static uint8_t arm_send_buf[RPMSG_MESSAGE_SIZE];
static enum bcam_cap_status run_state = BCAM_CAP_STOPPED;

/*
 * Disables PRU1 cycle counter in CTRL register.
 */
static void disable_timer()
{
	PRU1_CTRL.CTRL_bit.CTR_EN = 0;
}

/*
 * Resets the counter value in the PRU1 CYCLE register. Note this requires the
 * counter to be disabled. Afterwards (re)enables the counter in CTRL register.
 */
static void enable_timer()
{
	disable_timer();

	PRU1_CTRL.CYCLE = 0;
	PRU1_CTRL.CTRL_bit.CTR_EN = 1;
}

/*
 * Verifies if the counter value in the CYCLE register exceeded
 * the equivalent no. of cycles for the given timeout duration.
 *
 * Returns 0 if no timeout occurred. Otherwise, disables the timer
 * and returns 1.
 */
static int timer_expired(uint32_t tmout_usec)
{
	if (PRU1_CTRL.CYCLE < PRU_CYCLES_PER_USEC * tmout_usec)
		return 0;

	disable_timer();
	return 1;
}

/*
 * Utility to start/stop data capture on PRU0.
 * Returns 0 on success or -1 on failure.
 */
static int16_t start_stop_capture(uint8_t start)
{
	/* Prepare command for PRU0 */
	SMEM.pru1_cmd.id = PRU_CMD_NONE;
	SMEM.pru0_cmd.id = start ? PRU_CMD_CAP_START : PRU_CMD_CAP_STOP;

	/* Reset timer */
	enable_timer();

	/* Wait for ACK command from PRU0 */
	while (SMEM.pru1_cmd.id != PRU_CMD_ACK) {
		if (timer_expired(PRU0_ACK_TMOUT_USEC) != 0)
			return -1;
	}

	if (start) {
		WRITE_PIN(PIN_LED, HIGH);
		run_state = BCAM_CAP_STARTED;
		enable_timer();
	} else {
		WRITE_PIN(PIN_LED, LOW);
		run_state = BCAM_CAP_STOPPED;
		disable_timer();
	}

	return 0;
}

/*
 * Initializes PRU core.
 */
static void init_pru_core()
{
	uint8_t blinks;

	/*
	 * Allow OCP master port access by the PRU, so the PRU can read
	 * external memories.
	 */
	CT_CFG.SYSCFG_bit.STANDBY_INIT = 0;

	/* Clear the status of all interrupts */
	CT_INTC.SECR0 = 0xFFFFFFFF;
	CT_INTC.SECR1 = 0xFFFFFFFF;

	/* Set default frame acquisition configuration */
	SMEM.cap_config.xres = 160;
	SMEM.cap_config.yres = 120;
	SMEM.cap_config.bpp = 16;
	SMEM.cap_config.img_sz = SMEM.cap_config.xres * SMEM.cap_config.yres * SMEM.cap_config.bpp / 8;
	SMEM.cap_config.test_mode = 1;

	/* 3 Hz LED blink for 2 seconds */
	for (blinks = 0; blinks < 6; blinks++) {
		BLINK_LED(PIN_LED, 30);
	}
}

/*
 * Initializes RPMsg subsystem.
 */
static void init_rpmsg(struct pru_rpmsg_transport *transport, uint8_t reinit)
{
	volatile uint8_t *status;

	if (reinit == 1) {
		start_stop_capture(0);

		/* Attempt to destory the existing channel */
		pru_rpmsg_channel(RPMSG_NS_DESTROY, transport,
				  CHAN_NAME, CHAN_DESC, CHAN_PORT);
	}

	/* Wait for the rpmsgcam driver to be ready for RPMsg communication */
	status = &resourceTable.rpmsg_vdev.status;
	while (!(*status & VIRTIO_CONFIG_S_DRIVER_OK)) {
		BLINK_LED(PIN_LED, 20);
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
		BLINK_LED(PIN_LED, 10);
	}
}

/*
 * Sends info messages to ARM as requested via BCAM_ARM_MSG_GET_* commands.
 */
static int16_t rpmsg_send_info(struct pru_rpmsg_transport *transport, uint32_t src,
			       uint32_t dst, struct bcam_arm_msg *req_info_cmd)
{
	struct bcam_pru_msg *msg = (struct bcam_pru_msg *)arm_send_buf;
	const char *fw_ver = PRU_FW_VERSION;
	uint8_t *data_buf;

	msg->type = BCAM_PRU_MSG_INFO;
	data_buf = msg->info_hdr.data;

	switch (req_info_cmd->id) {
	case BCAM_ARM_MSG_GET_PRUFW_VER:
		while (*fw_ver != 0)
			*data_buf++ = *fw_ver++;
		*data_buf++ = 0;
		break;

	case BCAM_ARM_MSG_GET_CAP_STATUS:
		*data_buf++ = run_state;
		break;

	default:
		return PRU_RPMSG_NO_KICK;
	}

	return pru_rpmsg_send(transport, src, dst, arm_send_buf,
			      (uint32_t)data_buf - (uint32_t)arm_send_buf);
}

/*
 * Sends a log message to ARM.
 */
static int16_t rpmsg_send_log(struct pru_rpmsg_transport *transport, uint32_t src,
			      uint32_t dst, enum bcam_pru_log_level level, const char *str)
{
	struct bcam_pru_msg *msg = (struct bcam_pru_msg *)arm_send_buf;
	uint8_t *data_buf;

	msg->type = BCAM_PRU_MSG_LOG;
	msg->log_hdr.level = level;
	data_buf = msg->log_hdr.data;

	while ((*str != 0) && ((uint32_t)data_buf - (uint32_t)arm_send_buf
				< sizeof(arm_send_buf) - 1))
		*data_buf++ = *str++;

	*data_buf++ = 0;

	return pru_rpmsg_send(transport, src, dst, arm_send_buf,
			      (uint32_t)data_buf - (uint32_t)arm_send_buf);
}

/*
 * Re-implementation of pru_rpmsg_send() to optimize capture data transfer
 * by caching the data blocks until the total size of the data exceeds
 * RPMSG_MESSAGE_SIZE bytes.
 *
 * When that happens, the ARM host is kicked to process the current transmission
 * queue buffer and a new one will be used to store subsequent messages.
 *
 * To explicitly flush the cache, call the function with 'flush' argument set
 * to 1, which forces the data transfer.
 */
static int16_t rpmsg_send_cap(struct pru_rpmsg_transport *transport,
			      uint32_t src, uint32_t dst,
			      struct cap_data *cap, uint8_t flush)
{
	/* Standard RPMsg infrastructure data */
	static struct pru_rpmsg_hdr	*msg;
	static uint32_t			msg_len;
	static int16_t			head;
	static struct pru_virtqueue	*virtqueue;

	/* Transfer optimization data */
	static uint16_t			cached_len = 0;
	static uint16_t			bseq = 0;
	struct bcam_pru_msg		*bmsg;
	int16_t				ret;

	if (cap != NULL) {
		if (cached_len > 0) {
			bmsg = (struct bcam_pru_msg *)msg->data;

			/*
			 * Force transferring cached frame if its section
			 * changed or there is no room to append new content.
			 * Note cap->seq provides frame section type (see main loop).
			 */
			if (cap->seq != bmsg->cap_hdr.frm ||
					cached_len + cap->len > RPMSG_MESSAGE_SIZE) {
				ret = rpmsg_send_cap(transport, src, dst, NULL, 1);
				if (ret != PRU_RPMSG_SUCCESS)
					return ret;
			}
		}

		if (cached_len == 0) {
			/* Cache empty, get new pru queue buffer */
			virtqueue = &transport->virtqueue0;
			head = pru_virtqueue_get_avail_buf(virtqueue, (void **)&msg, &msg_len);
			if (head < 0)
				return PRU_RPMSG_NO_BUF_AVAILABLE;

			/* Setup new bmsg header */
			bmsg = (struct bcam_pru_msg *)msg->data;
			bmsg->type = BCAM_PRU_MSG_CAP;
			bmsg->cap_hdr.frm = cap->seq;

			if (cap->seq == BCAM_FRM_START)
				bseq = 0; /* Reset frame part seq for each new frame */
			bmsg->cap_hdr.seq = bseq++;

			cached_len = sizeof(bmsg->type) + sizeof(bmsg->cap_hdr);
		}

		/* Append current frame data */
		memcpy(msg->data + cached_len, cap->data, cap->len);
		cached_len += cap->len;
	}

	if (flush == 1 && cached_len > 0) {
		msg->len = cached_len;
		msg->dst = dst;
		msg->src = src;
		msg->flags = 0;
		msg->reserved = 0;

		/* Add the used buffer */
		if (pru_virtqueue_add_used_buf(virtqueue, head, msg_len) < 0) {
			cached_len = 0;
			return PRU_RPMSG_INVALID_HEAD;
		}

		/* Kick the ARM host */
		pru_virtqueue_kick(virtqueue);

		cached_len = 0;
		return PRU_RPMSG_SUCCESS;
	}

	return PRU_RPMSG_NO_KICK;
}

/*
 * Main loop.
 */
void main(void)
{
	struct pru_rpmsg_transport transport;
	uint16_t rpmsg_src, rpmsg_dst;

	struct bcam_arm_msg *arm_cmd = (struct bcam_arm_msg *)arm_recv_buf;
	uint16_t arm_cmd_len;

	struct cap_data capture_buf;
	uint32_t crt_frame_data_len;
	uint16_t exp_cap_seq, send_ret;
	uint8_t crt_bank;

	/* Initialization */
	init_pru_core();
	init_rpmsg(&transport, 0);

	/* Main loop */
	while (1) {
		/* Bit R31.31 is set when the ARM has kicked us */
		if (__R31 & HOST_INT) {
			/* Clear the event status */
			CT_INTC.SICR_bit.STS_CLR_IDX = FROM_ARM_HOST;

			/* Receive all available messages, multiple messages can be sent per kick */
			while (pru_rpmsg_receive(&transport, &rpmsg_src, &rpmsg_dst,
						 arm_recv_buf, &arm_cmd_len) == PRU_RPMSG_SUCCESS) {
				if (arm_cmd_len < sizeof(*arm_cmd) || (arm_cmd->magic_byte.high << 8
						| arm_cmd->magic_byte.low) != BCAM_ARM_MSG_MAGIC) {
					rpmsg_send_log(&transport, rpmsg_dst, rpmsg_src,
						       BCAM_PRU_LOG_DEBUG, "Malformed cmd");
					continue;
				}

				switch (arm_cmd->id) {
				case BCAM_ARM_MSG_GET_PRUFW_VER:
				case BCAM_ARM_MSG_GET_CAP_STATUS:
					rpmsg_send_info(&transport, rpmsg_dst, rpmsg_src, arm_cmd);
					break;

				case BCAM_ARM_MSG_CAP_SETUP:
					SMEM.cap_config.xres = ((struct bcam_cap_config *)arm_cmd->data)->xres;
					SMEM.cap_config.yres = ((struct bcam_cap_config *)arm_cmd->data)->yres;
					SMEM.cap_config.bpp = ((struct bcam_cap_config *)arm_cmd->data)->bpp;
					SMEM.cap_config.img_sz = SMEM.cap_config.xres * SMEM.cap_config.yres * SMEM.cap_config.bpp / 8;
					SMEM.cap_config.test_mode = ((struct bcam_cap_config *)arm_cmd->data)->test_mode;
					SMEM.cap_config.test_pclk_mhz = ((struct bcam_cap_config *)arm_cmd->data)->test_pclk_mhz;

					rpmsg_send_log(&transport, rpmsg_dst, rpmsg_src,
						       BCAM_PRU_LOG_INFO, "Capture configured");
					break;

				case BCAM_ARM_MSG_CAP_START:
					run_state = BCAM_CAP_PAUSED;
					rpmsg_send_log(&transport, rpmsg_dst, rpmsg_src,
						       BCAM_PRU_LOG_INFO, "Capture initiated");
					break;

				case BCAM_ARM_MSG_CAP_STOP:
					if (start_stop_capture(0) != 0) {
						run_state = BCAM_CAP_STOPPED;
						rpmsg_send_log(&transport, rpmsg_dst, rpmsg_src,
							       BCAM_PRU_LOG_ERROR, "Failed to stop capture");
					} else {
						rpmsg_send_log(&transport, rpmsg_dst, rpmsg_src,
							       BCAM_PRU_LOG_INFO, "Capture stopped");
					}
					break;

				default:
					rpmsg_send_log(&transport, rpmsg_dst, rpmsg_src,
						       BCAM_PRU_LOG_ERROR, "Unknown command");
				}
			}
		}

		if (run_state == BCAM_CAP_PAUSED) {
			if (SMEM.cap_config.test_mode == 0) {
				/* TODO: check VSYNC line */
				/* VSYNC not yet LOW, process pending ARM commands */
				continue;
			} else {
				/*
				 * Simulate VSYNC wait according to the OV7670 specs:
				 * (VSYNC - HREF) delay = 20 * HREF duration
				 */
				delay_cycles_var(20 * SMEM.cap_config.xres * (SMEM.cap_config.bpp / 8) *
						 PRU_CYCLES_PER_USEC / SMEM.cap_config.test_pclk_mhz);
			}

			/* Resume frame acquisition */
			if (start_stop_capture(1) != 0) {
				run_state = BCAM_CAP_STOPPED;
				rpmsg_send_log(&transport, rpmsg_dst, rpmsg_src,
					       BCAM_PRU_LOG_ERROR, "Failed to start/resume capture");
				continue;
			}

			crt_frame_data_len = 0;
			exp_cap_seq = 1;
			crt_bank = 0;
		}

		while (run_state == BCAM_CAP_STARTED) {
			if (timer_expired(PRU0_CAP_TMOUT_USEC) != 0) {
				start_stop_capture(0);

				/* Discard any cached frame data */
				capture_buf.seq = BCAM_FRM_INVALID;
				capture_buf.len = 0;
				rpmsg_send_cap(&transport, rpmsg_dst, rpmsg_src, &capture_buf, 1);

				rpmsg_send_log(&transport, rpmsg_dst, rpmsg_src,
					       BCAM_PRU_LOG_ERROR, "Timeout receiving data from PRU0");
				break;
			}

			/*
			 * Load data stored by PRU0 in the current scratch pad bank.
			 * Note using the switch statement as workaround for:
			 * "error #664: expected an integer constant"
			 */
			switch (crt_bank) {
			case 0:
				LOAD_DATA(0, capture_buf);
				break;
			case 1:
				LOAD_DATA(1, capture_buf);
				break;
			case 2:
				LOAD_DATA(2, capture_buf);
				break;
			}

			/*
			 * Continue to read the current scratch pad bank until
			 * the expected seq. no. is detected or timeout occurs.
			 */
			if (capture_buf.seq < exp_cap_seq)
				continue;

			/*
			 * We missed some previous frame data, inform ARM host
			 * to discard current frame by marking it invalid.
			 */
			if (capture_buf.seq > exp_cap_seq) {
				start_stop_capture(0);

				/* Discard any cached frame data */
				capture_buf.seq = BCAM_FRM_INVALID;
				rpmsg_send_cap(&transport, rpmsg_dst, rpmsg_src, &capture_buf, 1);

				rpmsg_send_log(&transport, rpmsg_dst, rpmsg_src, BCAM_PRU_LOG_ERROR,
					       "Unexpected seq from PRU0, discarding frame");

				/* Get next frame */
				run_state = BCAM_CAP_PAUSED;
				break;
			}

			crt_frame_data_len += capture_buf.len;

			if (crt_frame_data_len >= SMEM.cap_config.img_sz) {
				/* Received enough frame data, mark the FRM_END section */
				capture_buf.seq = BCAM_FRM_END;

				/* Discard any extra captured data */
				capture_buf.len -= crt_frame_data_len - SMEM.cap_config.img_sz;

				/* Force sending completed frame */
				if (rpmsg_send_cap(&transport, rpmsg_dst, rpmsg_src, &capture_buf, 1) != PRU_RPMSG_SUCCESS)
					rpmsg_send_log(&transport, rpmsg_dst, rpmsg_src,
						       BCAM_PRU_LOG_ERROR, "Failed to send cap data");

				/* Temporarily pause frame acquisition */
				if (start_stop_capture(0) != 0)
					rpmsg_send_log(&transport, rpmsg_dst, rpmsg_src,
						       BCAM_PRU_LOG_ERROR, "Failed to pause capture");

				run_state = BCAM_CAP_PAUSED;

				if (SMEM.cap_config.test_mode == 0) {
					/* TODO: Wait for VSYNC turning HIGH */
					USLEEP(1);
				}

				break;
			}

			/* Append new frame data to the transmission cache */
			capture_buf.seq = (exp_cap_seq > 1 ? BCAM_FRM_BODY : BCAM_FRM_START);
			send_ret = rpmsg_send_cap(&transport, rpmsg_dst, rpmsg_src, &capture_buf, 0);

			if (send_ret != PRU_RPMSG_NO_KICK && send_ret != PRU_RPMSG_SUCCESS) {
				start_stop_capture(0);
				rpmsg_send_log(&transport, rpmsg_dst, rpmsg_src,
					       BCAM_PRU_LOG_ERROR, "Failed to send cap data");
				break;
			}

			/* Prepare for receiving next data sequence */
			exp_cap_seq++;
			/* Move to next scratch bank */
			NEXT_BANK(crt_bank);
			/* Reset timer */
			enable_timer();
		}
	}
}
