/*
 * BeagleCam PRU RPMsg API.
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _BCAM_RPMSG_API_H
#define _BCAM_RPMSG_API_H

#include <stdint.h>

/* Discard ARM messages that do not start with this byte sequence. */
#define BCAM_ARM_MSG_MAGIC		0xbeca

/* Messages (commands) sent from ARM to PRU1. */
struct bcam_arm_msg {
	union {
		uint8_t magic[2];	/* Magic byte sequence */
		struct {
			uint8_t high;	/* Magic high byte */
			uint8_t low;	/* Magic low byte */
		} magic_byte;
	};
	uint8_t id;			/* Member of enum bcam_arm_msg_type */
	uint8_t data[0];		/* Message data (currently not used) */
} __attribute__((packed));

/* Messages sent from PRU1 to ARM. */
struct bcam_pru_msg {
	uint8_t type;				/* Member of enum bcam_pru_msg_type */

	union {
		/* BCAM_PRU_MSG_INFO type */
		struct __attribute__((packed)) {
			uint8_t data[0];	/* Command response */
		} info_hdr;

		/* BCAM_PRU_MSG_LOG type */
		struct __attribute__((packed)) {
			uint8_t level;		/* Member of enum bcam_pru_log_level */
			uint8_t data[0];	/* Message string */
		} log_hdr;

		/* BCAM_PRU_MSG_CAP type */
		struct __attribute__((packed)) {
			uint8_t frm;		/* Member of enum bcam_frm_sect */
			uint16_t seq;		/* Data sequence no. */
			uint8_t data[0];	/* Captured image data */
		} cap_hdr;
	};
} __attribute__((packed));

/* IDs for messages (commands) sent from ARM to PRU1. */
enum bcam_arm_msg_type {
	BCAM_ARM_MSG_GET_PRUFW_VER = 0,		/* Get PRU firmware version */
	BCAM_ARM_MSG_GET_CAP_STATUS,		/* Get camera capture status */
	BCAM_ARM_MSG_CAP_START,			/* Start camera data capture */
	BCAM_ARM_MSG_CAP_STOP,			/* Stop camera data capture */
};

/* IDs for messages sent from PRU1 to ARM. */
enum bcam_pru_msg_type {
	BCAM_PRU_MSG_NONE = 0,		/* Null data */
	BCAM_PRU_MSG_INFO,		/* BCAM_ARM_MSG_GET_* requested info */
	BCAM_PRU_MSG_LOG,		/* Log entry */
	BCAM_PRU_MSG_CAP,		/* Capture data */
};

/* Camera capture status. */
enum bcam_cap_status {
	BCAM_CAP_STOPPED = 0,
	BCAM_CAP_STARTED,
};

/* Log levels. */
enum bcam_pru_log_level {
	BCAM_PRU_LOG_FATAL = 0,
	BCAM_PRU_LOG_ERROR,
	BCAM_PRU_LOG_WARN,
	BCAM_PRU_LOG_INFO,
	BCAM_PRU_LOG_DEBUG,
};

/* Frame section. */
enum bcam_frm_sect {
	BCAM_FRM_NONE = 0,		/* Null frame */
	BCAM_FRM_START,			/* Frame start */
	BCAM_FRM_BODY,			/* Frame body */
	BCAM_FRM_END,			/* Frame end */
	BCAM_FRM_INVALID,		/* Frame invalid, should be discarded */
};

#endif /* _BCAM_RPMSG_API_H */
