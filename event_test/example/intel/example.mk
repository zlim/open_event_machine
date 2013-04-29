#
#  Copyright (c) 2013, Nokia Siemens Networks
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

PROJECT_ROOT      := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../..)
EVENT_MACHINE_DIR := $(PROJECT_ROOT)/event_machine
EVENT_TEST_DIR    := $(PROJECT_ROOT)/event_test
EXAMPLE_DIR       := $(EVENT_TEST_DIR)/example
EM_LIB_BUILD_DIR  := $(EVENT_MACHINE_DIR)/intel
EM_LIB_DIR        := $(EM_LIB_BUILD_DIR)/lib


# Default test example application is "hello" if not explicitly specified on the command line: 
#   > make -f example.mk APPL=[hello|perf|...]
APPL ?= hello



# binary name required by the DPDK build system
APP    = $(APPL)
# 
SRCS-y = 



EM_LIB = $(EM_LIB_DIR)/libem.a



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
CFLAGS += -DEM_64_BIT
# EM include dirs
CFLAGS += -I$(EVENT_MACHINE_DIR)
CFLAGS += -I$(EVENT_MACHINE_DIR)/intel
CFLAGS += -I$(EXAMPLE_DIR)
CFLAGS += -I$(EXAMPLE_DIR)/intel
CFLAGS += -I$(PROJECT_ROOT)/misc
CFLAGS += -I$(PROJECT_ROOT)/misc/intel



#
# Test Case Source Code
#

ALL_TEST_SRCS   =   
ALL_TEST_SRCS  += $(EXAMPLE_DIR)/intel/example_main.c

ifeq ($(APPL),hello)
ALL_TEST_SRCS  += $(EXAMPLE_DIR)/test_appl_hello.c
endif

ifeq ($(APPL),perf)
ALL_TEST_SRCS  += $(EXAMPLE_DIR)/test_appl_perf.c
endif

ifeq ($(APPL),event_group)
ALL_TEST_SRCS  += $(EXAMPLE_DIR)/test_appl_event_group.c
endif

ifeq ($(APPL),error)
ALL_TEST_SRCS  += $(EXAMPLE_DIR)/test_appl_error.c
endif



# Intel DPDK expects all sources to be in SRCS-y
SRCS-y += $(ALL_TEST_SRCS)


#
# Libraries and lib-paths
#
EXTRA_LDFLAGS += -L$(EXAMPLE_DIR)/intel
EXTRA_LDFLAGS += -L$(EM_LIB_DIR)
EXTRA_LDFLAGS += --start-group -lem --end-group

.PHONY: appl 
appl: all $(APP) $(EM_LIB)

$(APP): $(EM_LIB) # put dependency so that things build in right order


# contains the 'all' target
include $(RTE_SDK)/mk/rte.extapp.mk



$(EM_LIB):
	@echo "**************************************************"
	@echo "Making Event Machine lib"
	@echo "**************************************************"
	@$(MAKE) -f $(EVENT_MACHINE_DIR)/intel/em_intel_lib.mk S=$(EVENT_MACHINE_DIR)/intel O=$(EM_LIB_BUILD_DIR) M=em_intel_lib.mk


.PHONY: lib
lib: $(EM_LIB)


.PHONY: em_clean
em_clean:
	@$(MAKE) -f $(EVENT_MACHINE_DIR)/intel/em_intel_lib.mk S=$(EVENT_MACHINE_DIR)/intel O=$(EM_LIB_BUILD_DIR) M=em_intel_lib.mk real_clean
	

.PHONY: real_clean
real_clean: clean
	rm -fr build



