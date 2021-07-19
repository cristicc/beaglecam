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

/* Standard string conversion macros */
#define STR_HELPER(x)		#x
#define STR(x)				STR_HELPER(x)

/* Default camera resolution (QQVGA) */
#define DEFAULT_CAM_XRES	160
#define DEFAULT_CAM_YRES	120

/* Default Linux device paths */
#define DEFAULT_CAM_DEV		"/dev/i2c-1"	/* I2C2 on BeagleBone Black */
#define DEFAULT_FB_DEV		"/dev/fb0"
#define DEFAULT_RPMSG_DEV	"/dev/rpmsgcam31"

/* Program options */
#define PROG_OPT_STR		"l:c:f:r:s:h"

#define PROG_TRIVIAL_USAGE \
	"[-l LOG_LEVEL] [-x CAM_XRES -y CAM_YRES] [-c CAM_DEV]\n" \
	"		[-f FB_DEV] [-r RPMSG_DEV] [-s DUMP_FILE] [-h]"

#define PROG_FULL_USAGE "Options:" \
	"\n -l LOG_LEVEL	Console log level no (0 FATAL, 1 ERROR, 2 WARN, 3 INFO, 4 DEBUG, 5 TRACE)" \
	"\n -x CAM_XRES	Camera X resolution (default "STR(DEFAULT_CAM_XRES)")" \
	"\n -y CAM_YRES	Camera Y resolution (default "STR(DEFAULT_CAM_YRES)")" \
	"\n -c CAM_DEV	Camera I2C device path (default "DEFAULT_CAM_DEV")" \
	"\n -f FB_DEV	LCD display Frame Buffer device path (default "DEFAULT_FB_DEV")" \
	"\n -r RPMSG_DEV	RPMsg device path (default "DEFAULT_RPMSG_DEV")" \
	"\n -s DUMP_FILE	File path to save the first captured image frame" \

struct prog_opts {
	int log_level;
	int cam_xres;
	int cam_yres;
	const char *cam_dev;
	const char *fb_dev;
	const char *rpmsg_dev;
	const char *dump_file;
};

/* Utility to count elements in the ring buffer. */
#define CIRC_CNT(head, tail, size) (((head) - (tail)) & ((size) - 1))
/*
 * Utility to get the available space in the ring buffer.
 * Note there is always one free position as a completely full buffer has
 * head == tail, which is the same as empty.
 */
#define CIRC_SPACE(head, tail, size) CIRC_CNT((tail), ((head) + 1), (size))

/* Size of the frame ring buffer */
#define FRAME_RING_SIZE		8

struct ring_buffer {
	/* Data buffer */
	struct rpmsg_cam_frame *buf[FRAME_RING_SIZE];

	/* Frame buffer indexes for reader (tail) & writer (head) */
	_Atomic int reader;
	_Atomic int writer;

	/* Conditional variable to notify reader of new frames */
	pthread_cond_t frame_rdy;
	pthread_mutex_t frame_rdy_lock;
};

/*
 * The ring buffer storing frames received from the camera module.
 */
static struct ring_buffer frame_ring = {
	.reader = 0,
	.writer = 0,
	.frame_rdy = PTHREAD_COND_INITIALIZER,
	.frame_rdy_lock = PTHREAD_MUTEX_INITIALIZER,
};

/* Flag for stopping the application gracefully. */
static volatile sig_atomic_t prog_stopping = 0;

/* Utility to programatically stop the application. */
static void prog_stop()
{
	prog_stopping = 1;
}

/* Handler for SIGINT. */
static void signal_handler(int sig, siginfo_t *siginfo, void *arg)
{
	prog_stop();
}

/* Setup SIGINT handler. */
static int setup_signal_handler()
{
	struct sigaction sa;
	int ret;

    sa.sa_sigaction = &signal_handler;
    sa.sa_flags = 0;

    ret = sigemptyset(&sa.sa_mask);
	if (ret != 0) {
		log_fatal("Failed to initialize signal set: %s", strerror(errno));
		return ret;
	}

    ret = sigaction(SIGINT, &sa, NULL);
	if (ret != 0)
		log_fatal("Failed to setup signal handler: %s", strerror(errno));

	return ret;
}

/*
 * Handler for frames_acq_thread cleanup.
 */
static void acquire_frames_cleanup_handler(void *arg)
{
	log_info("Stopping frames acquisition thread");
	rpmsg_cam_stop((rpmsg_cam_handle_t)arg);
}

/*
 * Receives frames from the camera module into the ring buffer.
 * It acts as a single producer (writer).
 */
static void *acquire_frames(void *rpmsg_cam_h)
{
	static struct rpmsg_cam_frame dropped_frame;
	int head, tail, ret;

	log_info("Starting frames acquisition thread");
	pthread_cleanup_push(acquire_frames_cleanup_handler, rpmsg_cam_h);

	ret = rpmsg_cam_start(rpmsg_cam_h);
	if (ret != 0) {
		log_fatal("Failed to start camera frames capture");
		goto cleanup;
	}

	dropped_frame.handle = rpmsg_cam_h;

	while (1) {
		head = frame_ring.writer;
		tail = atomic_load_explicit(&frame_ring.reader, memory_order_relaxed);

		if (CIRC_SPACE(head, tail, FRAME_RING_SIZE) >= 1) {
			ret = rpmsg_cam_get_frame(frame_ring.buf[head]);
			if (ret == -1) {
				log_error("Failed to get frame: %d", ret);
				break;
			}
			if (ret < -1) {
				log_debug("Skipping frame due to error: %d", ret);
				continue; /* Ignore frame & sync errors */
			}

			log_debug("Received frame: seq=%d", frame_ring.buf[head]->seq);

			/* Finish writing data before incrementing head */
			atomic_store_explicit(&frame_ring.writer, (head + 1) & (FRAME_RING_SIZE - 1),
								  memory_order_release);

			/* Notify the consumer thread */
			ret = pthread_cond_signal(&frame_ring.frame_rdy);
			if (ret != 0)
				log_debug("pthread_cond_signal failed: %s", strerror(ret));

		} else {
			log_warn("Ring buffer full, dropping frame");

			ret = rpmsg_cam_get_frame(&dropped_frame);
			if (ret == -1) {
				log_error("Failed to get frame: %d", ret);
				break;
			}

			/* Add cancellation point */
			pthread_testcancel();
		}
	}

cleanup:
	/* Calls acquire_frames_cleanup_handler() on normal thread termination */
	pthread_cleanup_pop(1);
	prog_stop();

	return NULL;
}

/*
 * Handler for frames_disp_thread cleanup.
 */
static void display_frames_cleanup_handler(void *arg)
{
	log_info("Stopping FB display thread");
	pthread_mutex_unlock(&frame_ring.frame_rdy_lock);
}

/*
 * Sends frames to the FB as soon as they are ready.
 * It acts as a single consumer (reader).
 */
static void *display_frames(void *pg_opts)
{
	struct prog_opts *opts = (struct prog_opts *)pg_opts;
	int head, tail, ret;

	log_info("Starting FB display thread");

	/* Cond var mutex must be locked before calling pthread_cond_wait() */
	ret = pthread_mutex_lock(&frame_ring.frame_rdy_lock);
	if (ret != 0) {
		log_error("pthread_mutex_lock failed: %s", strerror(ret));
		prog_stop();
		return NULL;
	}

	pthread_cleanup_push(display_frames_cleanup_handler, NULL);
	fb_clear();

	while (1) {
		/* Ensure index is read before content at that index */
		head = atomic_load_explicit(&frame_ring.writer, memory_order_acquire);
		tail = frame_ring.reader;

		if (CIRC_CNT(head, tail, FRAME_RING_SIZE) >= 1) {
			/* Render image into the frame buffer */
			fb_write((uint16_t *)frame_ring.buf[tail]->pixels, opts->cam_xres, opts->cam_yres);

			if ((frame_ring.buf[tail]->seq == 0) && (opts->dump_file[0] != 0)) {
				ret = rpmsg_cam_dump_frame(frame_ring.buf[tail], opts->dump_file);
				if (ret == 0)
					log_info("Dumped frame to file: %s", opts->dump_file);
			}

			/* Finish consuming data before incrementing tail */
			atomic_store_explicit(&frame_ring.reader, (tail + 1) & (FRAME_RING_SIZE - 1),
								  memory_order_release);
		} else {
			/* Ring buffer empty, wait for new frames */
			ret = pthread_cond_wait(&frame_ring.frame_rdy, &frame_ring.frame_rdy_lock);
			if (ret != 0)
				log_debug("pthread_cond_wait failed: %s", strerror(ret));
		}
	}

	/* Calls display_frames_cleanup_handler() on normal thread termination */
	pthread_cleanup_pop(1);
	prog_stop();

	return NULL;
}

/*
 * Prints program help text.
 */
static void usage(char *prog_name, int full) {
	fprintf(stderr, "Usage: %s "PROG_TRIVIAL_USAGE"\n", prog_name);

	if (full)
		fprintf(stderr, PROG_FULL_USAGE"\n");
}

/*
 * Main entry point for PRU-based Camera FB display.
 */
int main(int argc, char *argv[])
{
	pthread_t frames_acq_thread, frames_disp_thread;
	rpmsg_cam_handle_t rpmsg_cam_h = NULL;
	int opt, ret;

	struct prog_opts options = {
		.log_level = LOG_INFO,
		.cam_xres = DEFAULT_CAM_XRES,
		.cam_yres = DEFAULT_CAM_YRES,
		.cam_dev = DEFAULT_CAM_DEV,
		.fb_dev = DEFAULT_FB_DEV,
		.rpmsg_dev = DEFAULT_RPMSG_DEV,
		.dump_file = "",
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

		case 'x':
			ret = strtol(optarg, NULL, 10);
			if (ret > 0)
				options.cam_xres = ret;
			break;

		case 'y':
			ret = strtol(optarg, NULL, 10);
			if (ret > 0)
				options.cam_yres = ret;
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

		case 's':
			options.dump_file = optarg;
			break;

		case 'h':
			usage(basename(argv[0]), 1);
			exit(EXIT_SUCCESS);

		default: /* '?' */
			usage(basename(argv[0]), 0);
			exit(EXIT_FAILURE);
		}
	}

	if (optind < argc) {
		usage(basename(argv[0]), 0);
		exit(EXIT_FAILURE);
	}

	/* Set log level */
	log_set_level(options.log_level);

	log_info("Starting rpmsgcam app");

	/* Setup the signal handler for stopping app gracefully */
    ret = setup_signal_handler();
	if (ret != 0)
		exit(EXIT_FAILURE);

	/* Configure the OV7670 Camera Module via the I2C-like interface */
	if (options.cam_dev[0] != '-') {
		log_info("Initializing camera module");
		ret = cam_init(options.cam_dev);
		if (ret != 0) {
			log_fatal("Failed to initialize camera module");
			goto free_ring;
		}
	}

	/* Initialize LCD frame buffer */
	if (options.fb_dev[0] != '-') {
		log_info("Initializing LCD frame buffer");
		ret = fb_init(options.fb_dev);
		if (ret != 0) {
			log_fatal("Failed to initialize frame buffer");
			goto free_ring;
		}
	}

	/* Initialize PRUs via RPMsg */
	log_info("Initializing PRUs for %dx%d frame acquisition",
			 options.cam_xres, options.cam_yres);
	rpmsg_cam_h = rpmsg_cam_init(options.rpmsg_dev, options.cam_xres, options.cam_yres);
	if (rpmsg_cam_h == NULL) {
		log_fatal("Failed to initialize RPMsg camera communication");
		ret = -1;
		goto free_ring;
	}

	/* Allocate memory for frames circular buffer */
	for (int i = 0; i < FRAME_RING_SIZE; i++) {
		frame_ring.buf[i] = malloc(sizeof(struct rpmsg_cam_frame));
		if (frame_ring.buf[i] == NULL) {
			log_fatal("Not enough memory");
			ret = -1;
			goto free_ring;
		}
		frame_ring.buf[i]->handle = rpmsg_cam_h;
	}

	log_debug("Creating frame display thread");
	ret = pthread_create(&frames_disp_thread, NULL, display_frames, &options);
	if (ret != 0) {
		log_fatal("Failed to create frame display thread: %s", strerror(ret));
		goto free_ring;
	}

	log_debug("Creating frame acquisition thread");
	ret = pthread_create(&frames_acq_thread, NULL, acquire_frames, rpmsg_cam_h);
	if (ret != 0) {
		log_fatal("Failed to create frame acquisition thread: %s", strerror(ret));
		pthread_cancel(frames_disp_thread);
		goto join_disp;
	}

	while (prog_stopping == 0)
		sleep(1);

	log_info("Stopping rpmsgcam app");

	pthread_cancel(frames_acq_thread);
	pthread_cancel(frames_disp_thread);

	pthread_join(frames_acq_thread, NULL);
join_disp:
	pthread_join(frames_disp_thread, NULL);

free_ring:
	for (int i = 0; i < FRAME_RING_SIZE; i++)
		free(frame_ring.buf[i]);

	rpmsg_cam_release(rpmsg_cam_h);
	fb_release();

	exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
