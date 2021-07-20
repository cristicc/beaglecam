/*
 * General I2C utilities.
 *
 * Based on i2cfunc.c from shabaz's iobb library:
 * https://github.com/shabaz123/iobb
 */

#ifndef _I2CFUNC_H
#define _I2CFUNC_H

/**
 * Gets an I2C file descriptor.
 *
 * @dev_path: I2C device path
 * @addr: 7-bit address (8-bit address in data-sheet right-shifted by 1)
 *
 * Return: file descriptor to be used all functions below.
 */
int i2c_open(const char *dev_path, unsigned char addr);

/*
 * These functions return -1 on error, otherwise return the number of bytes
 * read/written.
 * To perform a 'repeated start', use i2c_write_read() function which can
 * write some data and then immediately read data without a stop bit in between.
 */
int i2c_write(int handle, unsigned char* buf, unsigned int length);
int i2c_read(int handle, unsigned char* buf, unsigned int length);
int i2c_write_read(int handle,
				unsigned char addr_w, unsigned char *buf_w, unsigned int len_w,
				unsigned char addr_r, unsigned char *buf_r, unsigned int len_r);

int i2c_write_ignore_nack(int handle, unsigned char addr_w,
				unsigned char* buf, unsigned int length);
int i2c_read_no_ack(int handle, unsigned char addr_r,
				unsigned char* buf, unsigned int length);

int i2c_write_byte(int handle, unsigned char val);
int i2c_read_byte(int handle, unsigned char* val);

/*
 * These functions return -1 on error, otherwise return 0 on success.
 */
int i2c_close(int handle);

/*
 * Provides an inaccurate delay (may be useful for waiting for ADC etc).
 * The maximum delay is 999msec.
 */
int delay_ms(unsigned int msec);

#endif /* _I2CFUNC_H */
