#ifndef _FB_H
#define _FB_H

#include <stdint.h>

int init_fb(const char *dev_path);
void write_fb(uint8_t *pixels);

#endif /* _FB_H */
