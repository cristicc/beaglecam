#
# Makefile for rpmsgcam application.
#

PROJECT = rpmsgcam-app
INCLUDE_DIR = include

SOURCES = fb.c i2cfunc.c log.c main.c ov7670-ctrl.c rpmsg-cam.c
LIBS = -pthread

ALL_CPPFLAGS = -I $(INCLUDE_DIR) $(CPPFLAGS)
CFLAGS = -g -DLOG_USE_COLOR=1 -Wall

SED := $(shell which sed || type -p sed)

.PHONY: all clean

all: $(PROJECT)

$(PROJECT): $(SOURCES:%.c=%.o)
	$(CC) $(LIBS) $^ -o $@

clean:
	rm *.o *.d $(PROJECT)

.SUFFIXES:
.SUFFIXES: .c .d .o

%.o: %.c
	$(CC) -c $(ALL_CPPFLAGS) $(CFLAGS) $< -o $@

%.d: %.c
	$(SHELL) -ec "$(CC) -M $(ALL_CPPFLAGS) $< | $(SED) 's/$*\\.o[ :]*/$@ &/g' > $@"

include $(SOURCES:.c=.d)
