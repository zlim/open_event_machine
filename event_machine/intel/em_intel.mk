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
# Event Machine makefile
#


#
# Includes
#
CFLAGS += -I$(EVENT_MACHINE_DIR)
CFLAGS += -I$(EVENT_MACHINE_DIR)/intel
CFLAGS += -I$(PROJECT_ROOT)/misc/intel



#
# Defines
#

# 64 bit version of EM API
CFLAGS += -DEM_64_BIT
#CFLAGS_LOCAL  += -DEM_32_BIT

# Include packet IO
#CFLAGS += -DEVENT_PACKET # Set, if needed, in higher level Makefile

CFLAGS += -I$(EVENT_MACHINE_DIR)
CFLAGS += -I$(PROJECT_ROOT)/misc       # misc_packet.h misc_list.h
CFLAGS += -I$(PROJECT_ROOT)/misc/intel # environment.h


#
# Source files
#

# Event Machine
EM_SRCS   = $(EVENT_MACHINE_DIR)/intel/em_main.c
EM_SRCS  += $(EVENT_MACHINE_DIR)/intel/em_intel.c
EM_SRCS  += $(EVENT_MACHINE_DIR)/intel/em_intel_sched.c
EM_SRCS  += $(EVENT_MACHINE_DIR)/intel/em_intel_event_group.c
EM_SRCS  += $(EVENT_MACHINE_DIR)/intel/em_intel_queue_group.c
EM_SRCS  += $(EVENT_MACHINE_DIR)/intel/em_internal_event.c
EM_SRCS  += $(EVENT_MACHINE_DIR)/intel/em_error.c
# Packet-IO
EM_SRCS  += $(EVENT_MACHINE_DIR)/intel/em_intel_packet.c
# Misc
EM_SRCS  += $(PROJECT_ROOT)/misc/intel/intel_hw_init.c
EM_SRCS  += $(PROJECT_ROOT)/misc/intel/intel_environment.c



# Object files
EM_OBJS = $(foreach obj,$(notdir $(EM_SRCS:.c=.o)),$(OBJ_DIR)/$(obj))

# Dependencies
DEPS +=  $(EM_OBJS:.o=.d)


# Export - Add to the 'all source files' variable
ALL_SRCS += $(EM_SRCS)
SRCS-y   += $(EM_SRCS)
