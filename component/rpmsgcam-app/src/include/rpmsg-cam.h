/*
 * Capture image frames from the camera module via the RPMsg bus.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _RPMSG_CAP_H
#define _RPMSG_CAP_H

#include <stdint.h>

#define BCAM_FRAME_LEN				(160 * 120 * 2) /* QQVGA RGB565 */

struct rpmsg_cam_frame {
	uint32_t seq;					/* Frame sequence */
	uint8_t data[BCAM_FRAME_LEN];	/* Image content */
};

typedef void *rpmsg_cam_handle_t;

rpmsg_cam_handle_t rpmsg_cam_start(const char *rpmsg_dev_path);
void rpmsg_cam_stop(rpmsg_cam_handle_t handle);
int rpmsg_cam_get_frame(rpmsg_cam_handle_t handle, struct rpmsg_cam_frame* frame);
int rpmsg_cam_dump_frame(const char *file_path, struct rpmsg_cam_frame *frame);

#endif /* _RPMSG_CAP_H */
