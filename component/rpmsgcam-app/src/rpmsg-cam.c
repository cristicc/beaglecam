/*
 * Utility to read camera frames via the RPMsg bus.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "bcam-rpmsg-api.h"
#include "log.h"
#include "rpmsg-cam.h"

#define RPMSG_MESSAGE_SIZE		496
#define EP_MAX_EVENTS			1
#define EP_TIMEOUT_MSEC			1500
#define DEFAULT_IMG_BPP			16

/*
 * State of a RPMsg capture instance.
 * Not directly exposed to user API, which uses rpmsg_cam_handle_t instead.
 */
struct rpmsg_cam_handle {
	uint32_t img_xres;						/* Image X resolution */
	uint32_t img_yres;						/* Image Y resolution */
	uint32_t img_bpp;						/* Image bits per pixel */
	uint32_t img_sz;						/* Image size in bytes */
	uint32_t frame_cnt;						/* Counter for image frames */
	int rpmsg_fd;							/* RPMsg file descriptor */
	uint8_t rpmsg_buf[RPMSG_MESSAGE_SIZE];	/* RPMsg receive buffer */
	int ep_fd;								/* Epoll file descriptor */
	struct epoll_event ep_evs[EP_MAX_EVENTS]; /* Epoll event list */
};

/*
 * Reads a PRU cap frame message having the expected sequence number.
 * Additionally, receives INFO and LOG messages.
 * The caller can access the message content via data and len parameters.
 *
 * Returns:
 *  0: Received non-frame message, to be ignored
 * >0: Valid frame section: BCAM_FRM_START, BCAM_FRM_BODY or BCAM_FRM_END
 * -1: Read error
 * -2: Invalid frame section, need to discard current frame
 * -3: Unexpected sequence number
 */
static int rpmsg_cam_read_msg(struct rpmsg_cam_handle *h, int exp_seq,
							  int *len, uint8_t **data)
{
	struct bcam_pru_msg* msg = (struct bcam_pru_msg*)h->rpmsg_buf;
	int ret;

	log_trace("RPMSg start reading msg");

	ret = epoll_wait(h->ep_fd, h->ep_evs, EP_MAX_EVENTS, EP_TIMEOUT_MSEC);
	if (ret < 0) {
		log_error("RPMsg epoll error: %s", strerror(errno));
		return ret;
	}
	if (ret == 0) {
		log_error("RPMsg timeout");
		return -1;
	}

	*len = read(h->ep_evs[0].data.fd, h->rpmsg_buf, RPMSG_MESSAGE_SIZE);
	if (*len < 0) {
		log_error("RPMsg read error: %s", strerror(errno));
		return -1;
	}

	if (*len == 0) {
		log_debug("RPMsg empty read");
		return -1;
	}

	log_trace("RPMSg end reading msg: type=%d, len=%d", msg->type, *len);
	log_hexdump(h->rpmsg_buf, *len, 16, 8);

	switch (msg->type) {
	case BCAM_PRU_MSG_INFO:
		*len -= msg->info_hdr.data - h->rpmsg_buf;
		*data = msg->info_hdr.data;
		return 0;

	case BCAM_PRU_MSG_LOG:
		*len -= msg->log_hdr.data - h->rpmsg_buf;
		*data = msg->log_hdr.data;
		log_write(msg->log_hdr.level, "PRU", 1, "%.*s", *len, *data);
		return 0;

	case BCAM_PRU_MSG_CAP:
		*len -= msg->cap_hdr.data - h->rpmsg_buf;
		*data = msg->cap_hdr.data;
		if (msg->cap_hdr.frm == BCAM_FRM_NONE)
			return 0;
		if (msg->cap_hdr.frm >= BCAM_FRM_INVALID) {
			log_trace("Received invalid frame section");
			return -2;
		}
		if (msg->cap_hdr.seq != exp_seq) {
			log_trace("Received unexpected RPMsg cap seq: %d instead of %d",
					  msg->cap_hdr.seq, exp_seq);
			return -3;
		}
		return msg->cap_hdr.frm;
	}

	log_warn("Received unknown RPMsg type: 0x%x", msg->type);
	return 0;
}

/*
 * Utility to send a PRU command.
 */
static int rpmsg_cam_send_cmd(struct rpmsg_cam_handle *h, enum bcam_arm_msg_type cmd_id,
							  void *cmd_data, int cmd_data_len)
{
	uint8_t cmd_buf[RPMSG_MESSAGE_SIZE];
	struct bcam_arm_msg *pru_cmd = (struct bcam_arm_msg *)cmd_buf;
	int cmd_len, ret;

	if (h == NULL) {
		log_error("RPMsg camera not initialized, use rpmsg_cam_init()");
		return -1;
	}

	pru_cmd->magic_byte.high = BCAM_ARM_MSG_MAGIC >> 8;
	pru_cmd->magic_byte.low = BCAM_ARM_MSG_MAGIC & 0xff;
	pru_cmd->id = cmd_id;

	cmd_len = sizeof(struct bcam_arm_msg) + cmd_data_len;
	if (cmd_data_len > 0)
		memcpy(pru_cmd->data, cmd_data, cmd_data_len);

	ret = write(h->rpmsg_fd, cmd_buf, cmd_len);

	if (ret < 0) {
		log_error("Failed to send PRU command (id=%d): %s", cmd_id, strerror(errno));
		return ret;
	}

	if (ret != cmd_len) {
		log_error("Sent incomplete PRU cmd data (id=%d): %d out of %d bytes",
				  cmd_id, ret, cmd_len);
		return -1;
	}

	/* Expecting just a PRU log message as command response from PRU */
	for (ret = 1; ret != -1 && ret != 0;)
		ret = rpmsg_cam_read_msg(h, 0, &cmd_len, (void *)(&pru_cmd));

	return ret;
}

/*
 * Starts capturing frames via PRU.
 *
 * Returns an opaque handle to a dynamically allocated internal state
 * structure or NULL in case of an error.
 */
rpmsg_cam_handle_t rpmsg_cam_init(const char *rpmsg_dev_path,
								  int xres, int yres,
								  int test_mode, int test_pclk_mhz)
{
	struct bcam_cap_config setup_data;
	struct rpmsg_cam_handle *h;
	struct epoll_event ev;
	int i, ret;

	h = malloc(sizeof(*h));
	if (h == NULL) {
		log_fatal("Not enough memory");
		return NULL;
	}

	/* RPMsg device might not be ready, let's keep trying for up to 3000 ms */
	for (i = 0;; i++) {
		h->rpmsg_fd = open(rpmsg_dev_path, O_RDWR);
		if (h->rpmsg_fd >= 0)
			break;

		if (errno != ENOENT || i == 3000) {
			log_error("Failed to open %s: %s", rpmsg_dev_path, strerror(errno));
			free(h);
			return NULL;
		}

		if (i == 0)
			log_info("Waiting for device: %s", rpmsg_dev_path);

		usleep(1000);
	}

	h->ep_fd = epoll_create1(0);
	if (h->ep_fd < 0) {
		log_error("epoll_create failed: %s", strerror(errno));
		rpmsg_cam_release(h);
		return NULL;
	}

	ev.data.fd = h->rpmsg_fd;
	ev.events = EPOLLIN;
	ret = epoll_ctl(h->ep_fd, EPOLL_CTL_ADD, h->rpmsg_fd, &ev);
	if (ret != 0) {
		log_error("epoll_ctl failed: %s", strerror(errno));
		close(h->ep_fd);
		h->ep_fd = -1;
		rpmsg_cam_release(h);
		return NULL;
	}

	setup_data.xres = xres;
	setup_data.yres = yres;
	setup_data.bpp = DEFAULT_IMG_BPP;
	setup_data.test_mode = test_mode;
	setup_data.test_pclk_mhz = test_pclk_mhz;

	ret = rpmsg_cam_send_cmd(h, BCAM_ARM_MSG_CAP_SETUP, &setup_data, sizeof(setup_data));
	if (ret != 0) {
		rpmsg_cam_release(h);
		return NULL;
	}

	h->img_xres = setup_data.xres;
	h->img_yres = setup_data.yres;
	h->img_bpp = setup_data.bpp;
	h->img_sz = h->img_xres * h->img_yres * h->img_bpp / 8;
	h->frame_cnt = 0;

	return h;
}

/*
 * Starts capturing frames via PRU.
 */
int rpmsg_cam_start(rpmsg_cam_handle_t handle)
{
	return rpmsg_cam_send_cmd(handle, BCAM_ARM_MSG_CAP_START, NULL, 0);
}

/*
 * Stops the frame capture.
 */
int rpmsg_cam_stop(rpmsg_cam_handle_t handle)
{
	return rpmsg_cam_send_cmd(handle, BCAM_ARM_MSG_CAP_STOP, NULL, 0);
}

/*
 * Releases the internal state memory.
 */
int rpmsg_cam_release(rpmsg_cam_handle_t handle)
{
	struct rpmsg_cam_handle *h = (struct rpmsg_cam_handle *)handle;
	int ret;

	if (h == NULL)
		return 0;

	if (h->ep_fd >= 0) {
		ret = epoll_ctl(h->ep_fd, EPOLL_CTL_DEL, h->rpmsg_fd, NULL);
		if (ret != 0)
			log_error("epoll_ctl failed: %s", strerror(errno));

		ret = close(h->ep_fd);
		if (ret != 0)
			log_error("Failed to close epoll descriptor: %s", strerror(errno));
	}

	ret = close(h->rpmsg_fd);
	if (ret != 0)
		log_error("Failed to close RPMsg descriptor: %s", strerror(errno));

	free(h);
	return ret;
}

/*
 * Transfers a full image frame.
 * Note the caller must set the "handle" frame attribute to point
 * to the return value of rpmsg_cam_init().
 *
 * Returns:
 *  0: Successful transfer
 * -1: Read error
 * -2: Frame error
 * -3: Sync error
 */
int rpmsg_cam_get_frame(struct rpmsg_cam_frame* frame)
{
	struct rpmsg_cam_handle *h = (struct rpmsg_cam_handle *)(frame->handle);
	int seq = 0, cnt = 0, ret, data_len;
	uint8_t *data;

	log_debug("Synchronizing frame start section");

	/* Keep reading RPMsg packets until receiving a "frame start" section */
	while (seq == 0) {
		ret = rpmsg_cam_read_msg(h, seq, &data_len, &data);

		switch (ret) {
		case BCAM_FRM_START:
			if (data_len > h->img_sz) {
				log_debug("Received start frame section too large: %d vs. %d bytes",
						  data_len, h->img_sz);
				return -2;
			}
			memcpy(frame->pixels, data, data_len);
			cnt = data_len;
			seq = 1;
			log_debug("Received start frame section %d (len=%d)", seq, data_len);
			break;

		case -1:
			return ret;

		case -2:
		case 0:
			break;

		default:
			/* Abort when exceeding 2 * image size */
			cnt += data_len;
			if (cnt > 2 * h->img_sz) {
				log_debug("No frame start section within %d bytes received", cnt);
				return -3;
			}
		}
	}

	log_debug("Synchronizing frame end section");

	/* Read remaining frame messages until receiving a "frame end" section */
	while (1) {
		ret = rpmsg_cam_read_msg(h, seq, &data_len, &data);

		switch (ret) {
		case BCAM_FRM_START:
			log_debug("Received a new frame start section, reset current frame");
			seq = 0;
			cnt = 0;
		case BCAM_FRM_BODY:
		case BCAM_FRM_END:
			if (cnt + data_len > h->img_sz) {
				log_debug("Received frame too large: %d vs. %d bytes",
						  cnt + data_len, h->img_sz);
				return -2;
			}
			break;

		case 0:
			continue;

		default:
			log_debug("Aborting frame transfer at %d out of %d bytes (err=%d)",
					  cnt, h->img_sz, ret);
			return ret;
		}

		/* Copy message data to frame buffer */
		memcpy(frame->pixels + cnt, data, data_len);
		cnt += data_len;
		seq++;

		if (ret == BCAM_FRM_END) {
			if (cnt < h->img_sz) {
				log_debug("Received incomplete frame: %d out of %d bytes",
						  cnt, h->img_sz);
				return -2;
			}

			log_debug("Received end frame section %d (len=%d)", seq, data_len);
			frame->seq = h->frame_cnt++;
			break;
		}

		log_debug("Received body frame section %d (len=%d)", seq, data_len);
	}

	return 0;
}

/*
 * Utility to write the content of a frame to a file.
 * Note the caller must set the "handle" frame attribute to point
 * to the return value of rpmsg_cam_init().
 */
int rpmsg_cam_dump_frame(const struct rpmsg_cam_frame *frame,
						 const char *file_path)
{
	struct rpmsg_cam_handle *h = (struct rpmsg_cam_handle *)(frame->handle);
	FILE *f;
	int ret;

	f = fopen(file_path, "w");
	if (f == NULL) {
		log_error("Failed to open %s: %s", file_path, strerror(errno));
		return -1;
	}

	ret = fwrite(frame->pixels, h->img_sz, 1, f);
	if (ret != 1) {
		log_error("Failed to dump frame: %s", strerror(errno));
		return -1;
	}

	ret = fclose(f);
	if (ret != 0) {
		log_error("Failed to close %s: %s", file_path, strerror(errno));
		return ret;
	}

	return 0;
}
