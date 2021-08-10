#
# Makefile for rpmsgcam application.
#

PROJECT = rpmsgcam-app
INCLUDE_DIR = include

SOURCES = fb.c gpio-util.c i2c-util.c log.c main.c ov7670-i2c.c ov7670-regs.c rpmsg-cam.c
LIBS = -pthread

ALL_CPPFLAGS = -I $(INCLUDE_DIR) $(CPPFLAGS)
ALL_CFLAGS = -g -DLOG_USE_COLOR=1 -Wall $(CFLAGS)

SED := $(shell which sed || type -p sed)

.PHONY: all clean

all: $(PROJECT)

$(PROJECT): $(SOURCES:%.c=%.o)
	$(CC) $(LDFLAGS) $(LIBS) $^ -o $@

clean:
	rm *.o *.d $(PROJECT)

.SUFFIXES:
.SUFFIXES: .c .d .o

%.o: %.c
	$(CC) -c $(ALL_CPPFLAGS) $(ALL_CFLAGS) $< -o $@

%.d: %.c
	$(SHELL) -ec "$(CC) -M $(ALL_CPPFLAGS) $< | $(SED) 's/$*\\.o[ :]*/$@ &/g' > $@"

include $(SOURCES:.c=.d)
