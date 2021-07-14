/*
 * Capture image frames from the camera module via the RPMsg bus.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
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
	int frame_cnt;							/* Counter for image frames */
	int rpmsg_fd;							/* RPMsg file descriptor */
	uint8_t rpmsg_buf[RPMSG_MESSAGE_SIZE];	/* RPMsg receive buffer */
};

/*
 * Starts capturing frames via PRU.
 *
 * Returns an opaque handle to a dynamically allocated internal state
 * structure or NULL in case of an error.
 */
rpmsg_cam_handle_t rpmsg_cam_start(const char *rpmsg_dev_path)
{
	struct rpmsg_cam_handle *h;
	int ret;

	struct bcam_arm_msg start_cmd = {
		.magic_byte.high = BCAM_ARM_MSG_MAGIC >> 8,
		.magic_byte.low = BCAM_ARM_MSG_MAGIC & 0xff,
		.id = BCAM_ARM_MSG_CAP_START,
	};

	h = malloc(sizeof(*h));
	if (h == NULL) {
		log_fatal("Not enough memory");
		return h;
	}

	h->rpmsg_fd = open(rpmsg_dev_path, O_RDWR);
	if (h->rpmsg_fd < 0) {
		log_error("Failed to open PRU device '%s': %s",
				  rpmsg_dev_path, strerror(errno));
		goto fail_open;
	}

	ret = write(h->rpmsg_fd, &start_cmd, sizeof(start_cmd));
	if (ret < 0) {
		log_error("Failed to send PRU cap_start command: %s", strerror(errno));
		goto fail_write;
	}

	if (ret != sizeof(start_cmd)) {
		log_error("Failed to send full PRU cap_start command");
		goto fail_write;
	}

	h->frame_cnt = 0;
	return h;

fail_write:
	close(h->rpmsg_fd);

fail_open:
	free(h);
	return NULL;
}

/*
 * Stops the frame capture and releases the internal state memory.
 */
void rpmsg_cam_stop(rpmsg_cam_handle_t handle)
{
	struct rpmsg_cam_handle *h = (struct rpmsg_cam_handle *)handle;
	int ret;

	struct bcam_arm_msg stop_cmd = {
		.magic_byte.high = BCAM_ARM_MSG_MAGIC >> 8,
		.magic_byte.low = BCAM_ARM_MSG_MAGIC & 0xff,
		.id = BCAM_ARM_MSG_CAP_STOP,
	};

	if (h == NULL)
		return;

	ret = write(h->rpmsg_fd, &stop_cmd, sizeof(stop_cmd));
	if (ret < 0) {
		log_error("Failed to send PRU cap_stop command: %s", strerror(errno));
		goto fail_write;
	}

	if (ret != sizeof(stop_cmd))
		log_error("Failed to send full PRU cap_stop command");

fail_write:
	close(h->rpmsg_fd);
	free(h);
}

/*
 * Read a PRU cap frame message having the expected sequence number.
 * If exp_seq is negative, a PRU info message is expected instead.
 * The caller can access the content via data and len parameters.
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

	*len = read(h->rpmsg_fd, h->rpmsg_buf, RPMSG_MESSAGE_SIZE);
	if (*len < 0) {
		log_error("RPMsg read error: %s", strerror(errno));
		return -1;
	}

	if (*len == 0) {
		log_debug("RPMsg empty read");
		return -1;
	}

	switch (msg->type) {
	case BCAM_PRU_MSG_INFO:
		if (exp_seq > 0)
			log_warn("Received unexpected info RPMsg type");
		*len -= msg->info_hdr.data - h->rpmsg_buf;
		*data = msg->info_hdr.data;
		return 0;

	case BCAM_PRU_MSG_LOG:
		*len -= msg->log_hdr.data - h->rpmsg_buf;
		*data = msg->log_hdr.data;
		log_log2(msg->log_hdr.level, "PRU: %.*s", *len, *data);
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
 * Transfer a full image frame.
 * Returns:
 *  0: Successful transfer
 * -1: Read error
 * -2: Frame error
 * -3: Sync error
 */
int rpmsg_cam_get_frame(rpmsg_cam_handle_t handle, struct rpmsg_cam_frame* frame)
{
	struct rpmsg_cam_handle *h = (struct rpmsg_cam_handle *)handle;
	int seq = 0, cnt = 0, ret, data_len;
	uint8_t *data;

	log_debug("Synchronizing frame start");

	/* Keep reading RPMsg packets until receiving a "frame start" section */
	while (seq == 0) {
		ret = rpmsg_cam_read_msg(h, seq, &data_len, &data);

		switch (ret) {
		case BCAM_FRM_START:
			if (data_len > sizeof(frame->data)) {
				log_debug("Received start frame too large: %d vs. %d bytes",
						  data_len, sizeof(frame->data));
				return -2;
			}
			memcpy(frame->data, data, data_len);
			cnt = data_len;
			seq = 1;
			break;

		case -1:
			return ret;

		case -2:
		case 0:
			break;

		default:
			/* Abort when exceeding max frame length */
			cnt += data_len;
			if (cnt > sizeof(frame->data)) {
				log_debug("No frame start section within %d bytes received", cnt);
				return -3;
			}
		}
	}

	log_debug("Synchronizing frame end");

	/* Read remaining frame messages until receiving a "frame end" section */
	while (1) {
		ret = rpmsg_cam_read_msg(h, seq, &data_len, &data);

		switch (ret) {
		case BCAM_FRM_START:
			log_debug("Received a new frame start, reset current frame");
			seq = 0;
			cnt = 0;
		case BCAM_FRM_BODY:
		case BCAM_FRM_END:
			if (cnt + data_len > sizeof(frame->data)) {
				log_debug("Received frame too large: %d vs. %d bytes",
						  cnt + data_len, sizeof(frame->data));
				return -2;
			}
			break;

		case 0:
			continue;

		default:
			if (ret != -1)
				log_debug("Discarding frame on RPMsg error: %d", ret);
			return ret;
		}

		/* Copy message data to frame buffer */
		memcpy(frame->data + cnt, data, data_len);
		cnt += data_len;
		seq++;

		if (ret == BCAM_FRM_END) {
			if (cnt < sizeof(frame->data)) {
				log_debug("Received incomplete frame: %d vs. %d bytes",
						  cnt, sizeof(frame->data));
				return -2;
			}

			frame->seq = h->frame_cnt++;
			break;
		}
	}

	return 0;
}
