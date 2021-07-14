#ifndef _FB_H
#define _FB_H

#include <stdint.h>

int init_fb(char* fb_path);
void update_fb(uint8_t* pixbuf);

#endif /* _FB_H */
