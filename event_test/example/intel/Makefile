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

EXAMPLES    := hello        perf \
               event_group  error

BUILD_DIR = ./build

EXECUTABLES    := $(foreach example,$(EXAMPLES),$(BUILD_DIR)/$(example))
EXAMPLES_CLEAN := $(foreach example,$(EXAMPLES),$(example)_clean)


.PHONY: all
all: $(EXAMPLES)


.PHONY: $(EXAMPLES)
$(EXAMPLES): %: $(BUILD_DIR)/%


$(EXECUTABLES):
	@echo
	@echo "**************************************************"
	@echo "Building EM example - $@:"
	@echo "**************************************************"
	@$(MAKE) -f example.mk M=example.mk O=$(BUILD_DIR) APPL=$(notdir $@)

	
.PHONY: clean
clean: $(EXAMPLES_CLEAN)


.PHONY: $(EXAMPLES_CLEAN)
$(EXAMPLES_CLEAN):
	@$(MAKE) -s -f example.mk M=example.mk O=$(BUILD_DIR) APPL=$(@:%_clean=%) clean


.PHONY: real_clean
real_clean: clean
	@$(MAKE) -s -f example.mk M=example.mk O=$(BUILD_DIR) real_clean


.PHONY: em_clean
em_clean:
	@$(MAKE) -s -f example.mk M=example.mk O=$(BUILD_DIR) em_clean


