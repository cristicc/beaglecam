/*
 * Capture image frames from the camera module via the RPMsg bus.
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _RPMSG_CAP_H
#define _RPMSG_CAP_H

#include <stdint.h>

#define BCAM_FRAME_LEN_MAX	(640 * 480 * 2) /* VGA RGB565 */

typedef void *rpmsg_cam_handle_t;

struct rpmsg_cam_frame {
	rpmsg_cam_handle_t handle;			/* Link frame to handle */
	uint32_t seq;						/* Frame sequence */
	uint8_t pixels[BCAM_FRAME_LEN_MAX];	/* Image content */
};

rpmsg_cam_handle_t rpmsg_cam_init(const char *rpmsg_dev_path, int xres, int yres);
int rpmsg_cam_start(rpmsg_cam_handle_t handle);
int rpmsg_cam_stop(rpmsg_cam_handle_t handle);
int rpmsg_cam_release(rpmsg_cam_handle_t handle);
int rpmsg_cam_get_frame(struct rpmsg_cam_frame* frame);
int rpmsg_cam_dump_frame(const struct rpmsg_cam_frame *frame, const char *file_path);

#endif /* _RPMSG_CAP_H */
