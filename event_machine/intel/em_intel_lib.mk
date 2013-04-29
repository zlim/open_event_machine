#
#  Copyright (c) 2012, Nokia Siemens Networks
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#      * Redistributions of source code must retain the above copyright
#        notice, this list of conditions and the following disclaimer.
#      * Redistributions in binary form must reproduce the above copyright
#        notice, this list of conditions and the following disclaimer in the
#        documentation and/or other materials provided with the distribution.
#      * Neither the name of Nokia Siemens Networks nor the
#        names of its contributors may be used to endorse or promote products
#        derived from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
#  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
#  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY
#  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
#  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
#  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
#  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
#  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#


#
# Event Machine library makefile
#

# Project root taken from the path of this makefile because the DPDK build system seems to call this makefile twice:
#  1. make to create ./build/ directory, and then 
#  2. call make from the build directory (make -C build -f this_makefile ...)
PROJECT_ROOT      := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
EVENT_MACHINE_DIR := $(PROJECT_ROOT)/event_machine
EVENT_TIMER_DIR   := $(PROJECT_ROOT)/event_timer


# binary name
LIB    = libem.a
# 
SRCS-y = 



ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif


# Default target, can be overriden by command line or environment
RTE_TARGET ?= x86_64-default-linuxapp-gcc

include $(RTE_SDK)/mk/rte.vars.mk



#CFLAGS += $(WERROR_FLAGS)

# workaround for a gcc bug with noreturn attribute
# http://gcc.gnu.org/bugzilla/show_bug.cgi?id=12603
ifeq ($(CONFIG_RTE_TOOLCHAIN_GCC),y)
CFLAGS_main.o += -Wno-return-type
endif


EXTRA_CFLAGS += -O3 -g -fstrict-aliasing


# Enable packet IO
CFLAGS += -DEVENT_PACKET # Set, if packet_io is needed
# Enable event timer
CFLAGS += -DEVENT_TIMER  # Set, if event timer is needed


#
# Event Machine sources
#
include $(EVENT_MACHINE_DIR)/intel/em_intel.mk
SRCS-y += $(EM_SRCS)


#
# Event Timer sources
#
include $(EVENT_TIMER_DIR)/intel/em_timer.mk
SRCS-y += $(EVENT_TIMER_SRCS)


#
# Last
#
include $(RTE_SDK)/mk/rte.extlib.mk


.PHONY: real_clean
real_clean: clean
	rm -fr $(RTE_OUTPUT)/lib


