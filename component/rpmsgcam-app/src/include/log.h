/*
 * Simple console logging utility.
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _LOG_H
#define _LOG_H

#include <stdarg.h>

enum { LOG_FATAL = 0, LOG_ERROR, LOG_WARN, LOG_INFO, LOG_DEBUG, LOG_TRACE };

#define log_fatal(...) log_write(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) log_write(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...)  log_write(LOG_WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...)  log_write(LOG_INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define log_debug(...) log_write(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_trace(...) log_write(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)

void log_set_level(int level);

void log_write(int level, const char *file, int line, const char *fmt, ...);

int hexdump(void const *data, size_t length, int linelen, int chunklen);

#endif /* _LOG_H */
