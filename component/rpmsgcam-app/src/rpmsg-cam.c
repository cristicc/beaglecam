/*
 * Capture image frames from the camera module via the RPMsg bus.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bcam-rpmsg-api.h"
#include "log.h"
#include "rpmsg-cam.h"

#define RPMSG_MESSAGE_SIZE		496

/*
 * State of a RPMsg capture instance.
 * Not directly exposed to user API, which uses rpmsg_cam_handle_t instead.
 */
struct rpmsg_cam_handle {
	uint32_t img_xres;						/* Image X resolution */
	uint32_t img_yres;						/* Image Y resolution */
	uint32_t img_bpp;						/* Image bits per pixel */
	uint32_t img_sz;						/* img_xres * img_yres * img_bpp */
	uint32_t frame_cnt;						/* Counter for image frames */
	int rpmsg_fd;							/* RPMsg file descriptor */
	uint8_t rpmsg_buf[RPMSG_MESSAGE_SIZE];	/* RPMsg receive buffer */
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

	log_trace("RPMSg start reading msg");

	*len = read(h->rpmsg_fd, h->rpmsg_buf, RPMSG_MESSAGE_SIZE);
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
		if (msg->cap_hdr.frm >= BCAM_FRM_INVALID)
			return -2;
		return (msg->cap_hdr.seq == exp_seq ? msg->cap_hdr.frm : -3);
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

	/* Expecting just a PRU log message */
	ret = rpmsg_cam_read_msg(h, 0, &cmd_len, (void *)(&pru_cmd));
	if (ret != 0) {
		log_error("Unexpected PRU cmd response code: %d", ret);
		return -1;
	}

	return 0;
}

/*
 * Starts capturing frames via PRU.
 *
 * Returns an opaque handle to a dynamically allocated internal state
 * structure or NULL in case of an error.
 */
rpmsg_cam_handle_t rpmsg_cam_init(const char *rpmsg_dev_path, int xres, int yres)
{
	struct bcam_cap_config setup_data;
	struct rpmsg_cam_handle *h;
	int bpp = 2, ret;

	h = malloc(sizeof(*h));
	if (h == NULL) {
		log_fatal("Not enough memory");
		return NULL;
	}

	h->rpmsg_fd = open(rpmsg_dev_path, O_RDWR);
	if (h->rpmsg_fd < 0) {
		log_error("Failed to open %s: %s", rpmsg_dev_path, strerror(errno));
		free(h);
		return NULL;
	}

	setup_data.xres = xres;
	setup_data.yres = yres;
	setup_data.bpp = bpp;

	ret = rpmsg_cam_send_cmd(h, BCAM_ARM_MSG_CAP_SETUP, &setup_data, sizeof(setup_data));
	if (ret != 0) {
		rpmsg_cam_release(h);
		return NULL;
	}

	h->img_xres = xres;
	h->img_yres = yres;
	h->img_bpp = bpp;
	h->img_sz = xres * yres * bpp;
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

	ret = close(h->rpmsg_fd);
	if (ret != 0) {
		log_error("Failed to close RPMsg descriptor: %s", strerror(errno));
	}

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
			log_trace("Received start frame section");
			memcpy(frame->pixels, data, data_len);
			cnt = data_len;
			seq = 1;
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
			log_debug("Discarding frame on RPMsg error: %d", ret);
			return ret;
		}

		/* Copy message data to frame buffer */
		memcpy(frame->pixels + cnt, data, data_len);
		cnt += data_len;
		seq++;

		if (ret == BCAM_FRM_END) {
			if (cnt < h->img_sz) {
				log_debug("Received incomplete frame: %d vs. %d bytes", cnt, h->img_sz);
				return -2;
			}

			log_debug("Received end frame section: count=%d", seq);
			frame->seq = h->frame_cnt++;
			break;
		}
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
