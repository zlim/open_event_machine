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
#  Makefile
#
#

PROJECT_ROOT      := $(dir $(MAKEFILE_LIST))../../..
EVENT_MACHINE_DIR := $(PROJECT_ROOT)/event_machine
EVENT_TEST_DIR    := $(PROJECT_ROOT)/event_test
EXAMPLE_DIR       := $(EVENT_TEST_DIR)/example



# binary name
APP    = test_appl
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


#
# Event machine for Intel
#
include $(EVENT_MACHINE_DIR)/intel/em_intel.mk



#
# Choose Test Case (only _ONE_ of the defines below can be set at a time!)
#
CFLAGS += -DTEST_APPL_HELLO
#CFLAGS += -DTEST_APPL_PERF
#CFLAGS += -DTEST_APPL_EVENT_GROUP
#CFLAGS += -DTEST_APPL_ERROR



#
# Test Case Source Code
#
ALL_TEST_SRCS   = 
ALL_TEST_SRCS  += $(EXAMPLE_DIR)/example_test_init.c
ALL_TEST_SRCS  += $(EXAMPLE_DIR)/test_appl_hello.c
ALL_TEST_SRCS  += $(EXAMPLE_DIR)/test_appl_event_group.c
ALL_TEST_SRCS  += $(EXAMPLE_DIR)/test_appl_error.c
ALL_TEST_SRCS  += $(EXAMPLE_DIR)/test_appl_perf.c


# Intel RTE expects all sources to be in SRCS-y
SRCS-y += $(ALL_TEST_SRCS)


# Object files (all except example_init_%.o)
ALL_TEST_OBJS = $(foreach obj,$(notdir $(ALL_TEST_SRCS:.c=.o)),$(OBJ_DIR)/$(obj))


# Append to the all-souce-files variable (for tag-generation)
ALL_SRCS += $(ALL_TEST_SRCS)




#
# Last
#
include $(RTE_SDK)/mk/rte.extapp.mk


real_clean: clean
	rm -fr build




