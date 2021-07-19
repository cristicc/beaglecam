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

/**
 * Displays the content of a buffer in hexadecimal format.
 *
 * @data: buffer to display
 * @length: length of the buffer
 * @linelen: number of chars per output line
 * @chunklen: number of chars per chunk
 */
int hexdump(void const *data, size_t length, int linelen, int chunklen)
{
	char buffer[512];
	char *ptr;
	const void *inptr;
	int pos;
	int remaining = length;

	if (log_level < LOG_TRACE)
		return 0;

	inptr = data;

	/*
	 * hex/ascii gap (2 chars) + closing \0 (1 char)
	 * split = 4 chars (2 each for hex/ascii) * number of splits
	 * (hex = 3 chars, ascii = 1 char) * linelen number of chars
	 */
	if (sizeof(buffer) < (3 + (4 * (linelen / chunklen)) + (linelen * 4)))
		return -1;

	/* Loop through each line remaining */
	while (remaining > 0) {
		int lrem;
		int splitcount;
		ptr = buffer;

		/* Loop through the hex chars of this line */
		lrem = remaining;
		splitcount = 0;
		for (pos = 0; pos < linelen; pos++) {

			/* Split hex section if required */
			if (chunklen == splitcount++) {
				sprintf(ptr, "  ");
				ptr += 2;
				splitcount = 1;
			}

			/* If still remaining chars, output, else leave a space */
			if (lrem) {
				sprintf(ptr, "%.2x ", *((unsigned char *) inptr + pos));
				lrem--;
			} else {
				sprintf(ptr, "   ");
			}
			ptr += 3;
		}

		*ptr++ = ' ';
		*ptr++ = ' ';

		/* Loop through the ASCII chars of this line */
		lrem = remaining;
		splitcount = 0;
		for (pos = 0; pos < linelen; pos++) {
			unsigned char c;

			/* Split ASCII section if required */
			if (chunklen == splitcount++) {
				sprintf(ptr, "  ");
				ptr += 2;
				splitcount = 1;
			}

			if (lrem) {
				c = *((unsigned char *) inptr + pos);
				if (c > 31 && c < 127) {
					sprintf(ptr, "%c", c);
				} else {
					sprintf(ptr, ".");
				}
				lrem--;
			}
			ptr++;
		}

		*ptr = '\0';
		fprintf(stderr, "%s\n", buffer);

		inptr += linelen;
		remaining -= linelen;
	}

	return 0;
}
