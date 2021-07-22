/*
 * GPIO utility.
 *
 * Copyright (c) 2021 Cristian Ciocaltea <cristian.ciocaltea@gmail.com>
 */

#ifndef _GPIO_UTIL_H
#define _GPIO_UTIL_H

int gpioutil_line_request_output(const char *gpiochip_dev_path, int line_offset);
int gpioutil_line_set_value(int gpioline_fd, int value);

#endif /* _GPIO_UTIL_H */
