#ifndef _FB_H
#define _FB_H

#include <stdint.h>

int init_fb(const char *dev_path);
void write_fb(uint16_t *rgb565);
void release_fb();

#endif /* _FB_H */
