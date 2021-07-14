/*
 * Acquire image frames from the camera module via the RPMsg bus and render
 * the content on LCD via Linux Frame Buffer.
 *
 * Using the Linux kernel circular buffer concept [1] to pass frame data from
 * the reader thread to the writer thread responsible for displaying images.
 *
 * [1] https://www.kernel.org/doc/Documentation/circular-buffers.txt
 *
 * Copyright (C) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "fb.h"
#include "log.h"
#include "ov7670-ctrl.h"
#include "rpmsg-cam.h"

/* Default device paths */
#define DEFAULT_CAM_DEV		"/dev/i2c-1"	/* I2C2 on BeagleBone Black */
#define DEFAULT_FB_DEV		"/dev/fb0"
#define DEFAULT_RPMSG_DEV	"/dev/rpmsgcam31"

/* Program options */
#define PROG_OPT_STR		"l:c:f:r:h"
#define PROG_USAGE_FMT		"%s [-l log_level] [-c cam_dev] [-f fb_dev] [-r rpmsg_dev] [-h]"

struct prog_opts {
	int log_level;
	const char *cam_dev;
	const char *fb_dev;
	const char *rpmsg_dev;
};

/* Return count in ring buffer */
#define CIRC_CNT(head, tail, size) (((head) - (tail)) & ((size) - 1))
/*
 * Return space available in ring buffer. We always leave one free position as
 * a completely full buffer has head == tail, which is the same as empty.
 */
#define CIRC_SPACE(head, tail, size) CIRC_CNT((tail), ((head) + 1), (size))

/* Size of the frame ring buffer */
#define FRAME_RING_SIZE		8

struct ring_buffer {
	/* Data buffer */
	struct rpmsg_cam_frame* buf[FRAME_RING_SIZE];

	/* Frame buffer indexes for reader (tail) & writer (head) */
	_Atomic int reader;
	_Atomic int writer;

	/* Conditional variable to notify reader of new frames */
	pthread_cond_t frame_rdy;
	pthread_mutex_t frame_rdy_lock;
};

static struct ring_buffer frame_ring = {
	.reader = 0,
	.writer = 0,
	.frame_rdy = PTHREAD_COND_INITIALIZER,
	.frame_rdy_lock = PTHREAD_MUTEX_INITIALIZER,
};

/* RPMsg camera handle */
static rpmsg_cam_handle_t cam_handle;

/*
 * Program graceful stop
 */
static _Atomic int prog_stopping = 0;

void prog_stop()
{
	log_info("Stopping rpmsgcam app");

	atomic_store_explicit(&prog_stopping, 1, memory_order_relaxed);
}

static int is_prog_stopping()
{
	return atomic_load_explicit(&prog_stopping, memory_order_relaxed);
}

/*
 * Receive frames from the camera module into the ring buffer.
 * It acts as a single producer (writer).
 */
static void *acquire_frames(void *rpmsg_dev)
{
	int head, tail, ret;
	static struct rpmsg_cam_frame frame;

	log_info("Starting camera frames acquisition");

	cam_handle = rpmsg_cam_start((const char *)rpmsg_dev);
	if (cam_handle == NULL) {
		log_fatal("Failed to initialize PRU communication");
		return NULL;
	}

	while (1) {
		ret = rpmsg_cam_get_frame(cam_handle, &frame);

		if (is_prog_stopping())
			break;

		if (ret == -1) {
			log_error("Failed to get frame: %d", ret);
			prog_stop();
			break;
		}

		if (ret < -1)
			continue; /* Ignore frame & sync errors */

		head = frame_ring.writer;
		tail = atomic_load_explicit(&frame_ring.reader, memory_order_relaxed);

		if (CIRC_SPACE(head, tail, FRAME_RING_SIZE) >= 1) {
			/* Copy the newly-received frame into ring buffer */
			memcpy(frame_ring.buf[head], &frame, sizeof(struct rpmsg_cam_frame));

			/* Finish writing data before incrementing head */
			atomic_store_explicit(&frame_ring.writer, (head + 1) & (FRAME_RING_SIZE - 1),
								  memory_order_release);

			/* Notify the consumer thread */
			pthread_cond_signal(&frame_ring.frame_rdy);

			if (is_prog_stopping())
				break;
		} else {
			log_debug("Dropped frame");
		}
	}

	log_info("Stopping camera frames acquisition");
	rpmsg_cam_stop(cam_handle);

	/* Wake consumer thread which might be waiting for new frames */
	pthread_mutex_lock(&frame_ring.frame_rdy_lock);
	pthread_cond_signal(&frame_ring.frame_rdy);
	pthread_mutex_unlock(&frame_ring.frame_rdy_lock);

	return NULL;
}

/*
 * Send frames to the FB as soon as they are ready.
 * It acts as a single consumer (reader).
 */
static void *display_frames(void *fb_dev)
{
	int head, tail;

	/* Initialize FB */
	if (init_fb((const char *)fb_dev) != 0) {
		log_fatal("Failed to initialize frame buffer");
		prog_stop();
		return NULL;
	}

	/* Cond var mutex must be locked before calling pthread_cond_wait() */
	pthread_mutex_lock(&frame_ring.frame_rdy_lock);

	while (1) {
		/* Ensure index is read before content at that index */
		head = atomic_load_explicit(&frame_ring.writer, memory_order_acquire);
		tail = frame_ring.reader;

		if (CIRC_CNT(head, tail, FRAME_RING_SIZE) >= 1) {
			/* Render image into the frame buffer */
			write_fb(frame_ring.buf[tail]->data);

			/* Finish consuming data before incrementing tail */
			atomic_store_explicit(&frame_ring.reader, (tail + 1) & (FRAME_RING_SIZE - 1),
								  memory_order_release);
		} else {
			/* Ring buffer empty, wait for new frames */
			pthread_cond_wait(&frame_ring.frame_rdy, &frame_ring.frame_rdy_lock);
		}

		if (is_prog_stopping())
			break;
	}

	pthread_mutex_unlock(&frame_ring.frame_rdy_lock);

	return NULL;
}

/*
 * Handler for SIGINT for gracefully close the app.
 */
static void handle_signal(int sig)
{
	prog_stop();
}

static void usage(char *prog_name) {
	fprintf(stderr, PROG_USAGE_FMT "\n", prog_name);
}

/*
 * Main entry point for PRU-based Camera FB display.
 */
int main(int argc, char *argv[])
{
	pthread_t frames_acq_thread, frames_disp_thread;
	int opt, ret;

	struct prog_opts options = {
		.log_level = LOG_INFO,
		.cam_dev = DEFAULT_CAM_DEV,
		.fb_dev = DEFAULT_FB_DEV,
		.rpmsg_dev = DEFAULT_RPMSG_DEV,
	};

	while ((opt = getopt(argc, argv, PROG_OPT_STR)) != -1) {
		switch (opt) {
		case 'l':
			ret = strtol(optarg, NULL, 10);
			if (ret < LOG_FATAL)
				options.log_level = LOG_FATAL;
			else if (ret > LOG_TRACE)
				options.log_level = LOG_TRACE;
			else
				options.log_level = ret;
			break;

		case 'c':
			options.cam_dev = optarg;
			break;

		case 'f':
			options.fb_dev = optarg;
			break;

		case 'r':
			options.rpmsg_dev = optarg;
			break;

		case 'h':
			usage(basename(argv[0]));
			exit(EXIT_SUCCESS);

		default: /* '?' */
			usage(basename(argv[0]));
			exit(EXIT_FAILURE);
		}
	}

	if (optind < argc) {
		usage(basename(argv[0]));
		exit(EXIT_FAILURE);
	}

	/* Set log level */
	log_set_level(options.log_level);

	log_info("Starting rpmsgcam app");

	/* Allocate memory for frames circular buffer */
	for (int i = 0; i < FRAME_RING_SIZE; i++)
		frame_ring.buf[i] = malloc(sizeof(struct rpmsg_cam_frame));

	/* Configure the OV7670 Camera Module via the I2C-like interface */
	if (options.cam_dev[0] != '-') {
		ret = cam_init(options.cam_dev);
		if (ret != 0)
			goto fail;
	}

	/* Setup the signal handler */
	signal(SIGINT, handle_signal);

	log_info("Creating frame acquisition thread");
	ret = pthread_create(&frames_acq_thread, NULL, acquire_frames, (void *)options.rpmsg_dev);
	if (ret != 0) {
		errno = ret;
		log_fatal("Failed to create thread: %s", strerror(errno));
		goto fail;
	}

	log_info("Creating frame display thread");
	ret = pthread_create(&frames_disp_thread, NULL, display_frames, (void *)options.fb_dev);
	if (ret != 0) {
		errno = ret;
		log_fatal("Failed to create thread: %s", strerror(errno));
		prog_stop();
		pthread_cancel(frames_acq_thread);
		pthread_join(frames_acq_thread, NULL);
		goto fail;
	}

	while (!is_prog_stopping())
		sleep(1);

	pthread_cancel(frames_acq_thread);
	pthread_cancel(frames_disp_thread);

	pthread_join(frames_acq_thread, NULL);
	pthread_join(frames_disp_thread, NULL);

	exit(EXIT_SUCCESS);

fail:
	exit(EXIT_FAILURE);
}
