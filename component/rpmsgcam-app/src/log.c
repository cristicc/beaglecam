/*
 * Simple console logging utility.
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "log.h"

#define LOG_LINE_MAX_LEN	1024

static int log_level = LOG_INFO;

static const char *log_level_names[] = {
	"FATAL", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"
};

#ifdef LOG_USE_COLOR
static const char *log_level_colors[] = {
	"\x1b[1;31m", "\x1b[31m", "\x1b[33m", "\x1b[32m", "\x1b[36m", "\x1b[94m"
};
#endif

void log_set_level(int level) {
	log_level = level;
}

void log_write(int level, const char *file, int line, const char *fmt, ...) {
	if (level > log_level)
		return;

	va_list args;
	char msg[LOG_LINE_MAX_LEN];
	char *chunk;
	time_t t = time(NULL);
	struct tm *lt = localtime(&t);

	chunk = msg + strftime(msg, 24, "%Y-%m-%d %H:%M:%S", lt);

#ifdef LOG_USE_COLOR
	chunk += snprintf(chunk, LOG_LINE_MAX_LEN / 2 - (chunk - msg),
					  " %s%-5s\x1b[0m \x1b[90m%s:%d:\x1b[0m ",
					  log_level_colors[level], log_level_names[level], file, line);
#else
	chunk += snprintf(chunk, LOG_LINE_MAX_LEN / 2 - (chunk - msg),
					  "%s %-5s %s:%d: ", log_level_names[level], file, line);
#endif

	va_start(args, fmt);
	chunk += vsnprintf(chunk, LOG_LINE_MAX_LEN - (chunk - msg), fmt, args);
	va_end(args);

	*chunk++ = '\n';

	fwrite(msg, chunk - msg, 1, stderr);
}
