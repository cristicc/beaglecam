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
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>

#include "fb.h"
#include "log.h"
#include "ov7670-ctrl.h"
#include "rpmsg-cam.h"

/* Size of the frame ring buffer */
#define FRAME_RING_SIZE		8

/* Linux devices */
#define CAM_I2C_DEVNO		1	/* I2C2 enumerated as 1 on BeagleBone Black */
#define RPMSG_DEV			"/dev/rpmsgcam31"
#define FB_DEV				"/dev/fb0"

/* Return count in ring buffer */
#define CIRC_CNT(head, tail, size) (((head) - (tail)) & ((size) - 1))
/*
 * Return space available in ring buffer. We always leave one free position as
 * a completely full buffer has head == tail, which is the same as empty.
 */
#define CIRC_SPACE(head, tail, size) CIRC_CNT((tail), ((head) + 1), (size))

/* Frame buffer indexes for reader (tail) & writer (head) */
static _Atomic int reader = 0;
static _Atomic int writer = 0;

/* Tracks the no. of available frames */
static sem_t frame_sem;

/* RPMsg camera handle */
static rpmsg_cam_handle_t cam_handle;

/* The frame buffer */
static struct rpmsg_cam_frame* frame_ring[FRAME_RING_SIZE];

/*
 * Read frames from the camera module into the ring buffer.
 * It acts as a single producer.
 */
static void* acquire_frames(void* prudev_path_ptr)
{
	char* prudev_path = (char*)prudev_path_ptr;
	int head, tail, ret;
	static struct rpmsg_cam_frame frame;

	log_debug("Initializing camera capture");

	cam_handle = rpmsg_cam_start(prudev_path);
	if (cam_handle == NULL) {
		log_error("Failed to start PRU frame acquisition");
		return NULL;
	}

	log_info("Starting camera frames transfer");

	while (1) {
		ret = rpmsg_cam_get_frame(cam_handle, &frame);

		if (ret == -1) {
			log_error("Failed to get frame: %d", ret);
			exit(-1);
		}

		if (ret < -1)
			continue; /* Ignore frame & sync errors */

		head = writer;
		tail = atomic_load_explicit(&reader, memory_order_relaxed);

		if (CIRC_SPACE(head, tail, FRAME_RING_SIZE) >= 1) {
			/* Copy the newly-received frame into ring buffer */
			memcpy(frame_ring[head], &frame, sizeof(struct rpmsg_cam_frame));

			/* Finish writing data before incrementing head */
			atomic_store_explicit(&writer, (head + 1) & (FRAME_RING_SIZE - 1),
								  memory_order_release);

			/* Post the space semaphore */
			sem_post(&frame_sem);
		} else {
			log_debug("Dropped frame");
		}
	}

	return NULL;
}

/*
 * Send frames to the FB as soon as they are ready.
 * It acts as a single consumer.
 */
static void* display_frames(void* fbdev_path_ptr)
{
	char* fbdev_path = (char*)fbdev_path_ptr;
	int head, tail;

	/* Initialize frame buffer */
	init_fb(fbdev_path);

	while (1) {
		/* Wait if there are no new frames to transmit */
		sem_wait(&frame_sem);

		/* Ensure index is read before content at that index */
		head = atomic_load_explicit(&writer, memory_order_acquire);
		tail = reader;

		if (CIRC_CNT(head, tail, FRAME_RING_SIZE) >= 1) {
			/* Render image into the frame buffer */
			update_fb(frame_ring[tail]->data);

			/* Finish reading data before incrementing tail */
			atomic_store_explicit(&reader, (tail + 1) & (FRAME_RING_SIZE - 1),
								  memory_order_release);

		}
	}

	return NULL;
}

/*
 * Handler for SIGINT for gracefully close the app.
 */
static void handle_signal(int sig)
{
	/* Try to shut down the PRUs before exiting */
	log_info("Stopping rpmsgcam app");

	rpmsg_cam_stop(cam_handle);
	sleep(1);	/* Ensure command is processed by the PRU */

	exit(0);
}

/*
 * Main entry point for PRU-based Camera FB display.
 */
int main(int argc, char *argv[])
{
	pthread_t frames_acq_thread, frames_disp_thread;
	int ret;

	/* Set default log level */
	log_set_level(LOG_INFO);

	log_info("Starting rpmsgcam app");

	/* Setup semaphores */
	ret = sem_init(&frame_sem, 0, 0);
	if (ret != 0) {
		log_fatal("Error initializing frame semaphore: %s", strerror(errno));
		return 1;
	}

	/* Allocate memory for frames circular buffer */
	for (int frame = 0; frame < FRAME_RING_SIZE; frame ++) {
		frame_ring[frame] = malloc(sizeof(struct rpmsg_cam_frame));
	}

	/* Configure the OV7670 Camera Module via the I2C-like interface */
	ret = cam_init(CAM_I2C_DEVNO);
	if (ret != 0)
		goto fail;

	/* Setup the signal handler */
	signal(SIGINT, handle_signal);

	log_info("Creating frames acquisition thread");
	ret = pthread_create(&frames_acq_thread, NULL, acquire_frames, RPMSG_DEV);
	if (ret != 0) {
		errno = ret;
		log_fatal("Error creating frames acquisition thread: %s", strerror(errno));
		goto fail;
	}

	log_info("Creating frames display thread");
	ret = pthread_create(&frames_disp_thread, NULL, display_frames, FB_DEV);
	if (ret != 0) {
		rpmsg_cam_stop(cam_handle);
		sleep(1);
		errno = ret;
		log_fatal("Error creating frames display thread: %s", strerror(errno));
		goto fail;
	}

	pthread_join(frames_acq_thread, NULL);
	pthread_join(frames_disp_thread, NULL);

fail:
	sem_destroy(&frame_sem);

	return ret;
}
