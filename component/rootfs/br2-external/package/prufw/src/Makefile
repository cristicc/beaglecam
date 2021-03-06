#
# Makefile based on the examples in the pru-software-support package.
#

ifndef PRU_CGT
$(error PRU_CGT environment variable is not set)
endif

MKFILE_PATH := $(abspath $(lastword $(MAKEFILE_LIST)))
CURRENT_DIR := $(notdir $(patsubst %/,%,$(dir $(MKFILE_PATH))))
PROJ_NAME=$(CURRENT_DIR)
LINKER_COMMAND_FILE=./AM335x_PRU.cmd

# Set paths according to HOST_PRU_SOFTWARE_SUPPORT_INSTALL_CMDS
LIBS=--library=$(PRU_CGT)/usr/lib/rpmsg_lib.lib
INCLUDE=--include_path=$(PRU_CGT)/usr/include --include_path=$(PRU_CGT)/usr/include/am335x

STACK_SIZE=0x100
HEAP_SIZE=0x100
GEN_DIR=gen

#Common compiler and linker flags (Defined in 'PRU Optimizing C/C++ Compiler User's Guide)
ALL_CFLAGS=-v3 -O2 --display_error_number --endian=little --hardware_mac=on --obj_directory=$(GEN_DIR) --pp_directory=$(GEN_DIR) -ppd -ppa $(CFLAGS)
#Linker flags (Defined in 'PRU Optimizing C/C++ Compiler User's Guide)
LFLAGS=--reread_libs --warn_sections --stack_size=$(STACK_SIZE) --heap_size=$(HEAP_SIZE)

TARGET0=$(GEN_DIR)/$(PROJ_NAME)0.out
MAP0=$(GEN_DIR)/$(PROJ_NAME)0.map
SOURCES0=main-pru0.c delay-cycles-var.asm
OBJECTS0:=$(patsubst %.c,$(GEN_DIR)/%.object,$(SOURCES0))
OBJECTS0:=$(patsubst %.asm,$(GEN_DIR)/%.object,$(OBJECTS0))

TARGET1=$(GEN_DIR)/$(PROJ_NAME)1.out
MAP1=$(GEN_DIR)/$(PROJ_NAME)1.map
SOURCES1=main-pru1.c delay-cycles-var.asm
OBJECTS1:=$(patsubst %.c,$(GEN_DIR)/%.object,$(SOURCES1))
OBJECTS1:=$(patsubst %.asm,$(GEN_DIR)/%.object,$(OBJECTS1))

all: $(TARGET0) $(TARGET1)

# Invokes the linker (-z flag) to make the .out file
$(TARGET0): $(OBJECTS0) $(LINKER_COMMAND_FILE)
	@echo 'Building target: $@'
	$(PRU_CGT)/bin/clpru $(ALL_CFLAGS) -z -i$(PRU_CGT)/lib -i$(PRU_CGT)/include $(LFLAGS) -o $(TARGET0) $(OBJECTS0) -m$(MAP0) $(LINKER_COMMAND_FILE) --library=libc.a

# Invokes the linker (-z flag) to make the .out file
$(TARGET1): $(OBJECTS1) $(LINKER_COMMAND_FILE)
	@echo 'Building target: $@'
	$(PRU_CGT)/bin/clpru $(ALL_CFLAGS) -z -i$(PRU_CGT)/lib -i$(PRU_CGT)/include $(LFLAGS) -o $(TARGET1) $(OBJECTS1) -m$(MAP1) $(LINKER_COMMAND_FILE) --library=libc.a $(LIBS)

# Invokes the compiler on all asm files in the directory to create the object files
$(GEN_DIR)/%.object: %.asm
	@mkdir -p $(GEN_DIR)
	$(PRU_CGT)/bin/clpru --include_path=$(PRU_CGT)/include $(INCLUDE) $(ALL_CFLAGS) -fe $@ $<

# Invokes the compiler on all c files in the directory to create the object files
$(GEN_DIR)/%.object: %.c $(wildcard *.h)
	@mkdir -p $(GEN_DIR)
	$(PRU_CGT)/bin/clpru --include_path=$(PRU_CGT)/include $(INCLUDE) $(ALL_CFLAGS) -fe $@ $<

# Remove the $(GEN_DIR) directory
clean:
	@echo 'Removing files in the "$(GEN_DIR)" directory'
	@rm -rf $(GEN_DIR)

.PHONY: all clean

# Includes the dependencies that the compiler creates (-ppd and -ppa flags)
-include $(OBJECTS:%.object=%.pp)
