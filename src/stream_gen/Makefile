#   BSD LICENSE
#
#   Copyright(c) 2010-2014 Intel Corporation. All rights reserved.
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions
#   are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#     * Neither the name of Intel Corporation nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


### Target ###
STREAMGEN_LIB=libsgen.a
### GCC ###
GCC=gcc
### SOURCES ###
SRCS=gen_api.c stream_gen.c string_matcher.c
OBJS=$(SRCS:.c=.o)

### ENVIRONMENT VARIABLES  ###
ifeq ($(RTE_SDK),)
$(error "Please define RTE_SDK environment variable")
endif

# Default target, can be overridden by command line or environment
RTE_TARGET ?= x86_64-native-linuxapp-gcc

include $(RTE_SDK)/mk/rte.vars.mk

### LIBRARIES AND INCLUDES ###
LDLIBS     = 

PCAP_CFLAGS = -I/usr/local/include
PCAPLIB     = -L/usr/local/lib -lpcap

LNET_CFLAGS = -I/usr/local/include 
LNETLIB     = -lnet

LIBS_CFLAGS = -Ilibnids-1.24/src -Iinclude  $(PCAP_CFLAGS) $(LNET_CFLAGS) 

## Enable statistics
LIBS_CFLAGS += -DSTAT_THREAD

## Only store request data
# LIBS_CFLAGS += -DONLY_REQUEST

## Out-of-order generation support
# LIBS_CFLAGS += -DOOO_SEND

## Dump payload data into seperate files according to tuple-4
## During generation process
# LIBS_CFLAGS += -DDUMP_PAYLOAD

## Dump payload data into seperate files according to tuple-4
## Defore generation process
# LIBS_CFLAGS += -DDUMP_PAYLOAD_ALL

## USE original tuple4
# LIBS_CFLAGS += -DORIGINAL_TUPLE4

## stream data only used for one time
# LIBS_CFLAGS += -DNON_REUSE

## Do not send ACK after a data packet
#LIBS_CFLAGS += -DNO_ACK

## Enable dpdk-pdump	
# LIBS_CFLAGS += -DUSE_PDUMP

## Generator with multiple threads
LIBS_CFLAGS += -DSEND_THREAD

## Generation with only one TCP stream
# LIBS_CFLAGS += -DSINGLE_STREAM

CFLAGS += -g -O2 $(LIBS_CFLAGS)
#CFLAGS 		+= $(WERROR_FLAGS)

LDLIBS += -Llibnids-1.24/src -lnids -lglib-2.0 $(PCAPLIB) $(LNETLIB) -lgthread-2.0 -lnsl -lpthread

### GOALS ###
all: $(SRCS) $(STREAMGEN_LIB)

.cpp.o:
	$(GCC) $(CFLAGS) $< -o $@

$(STREAMGEN_LIB): $(OBJS)
	ar rvs ../../lib/$@ $(OBJS)
	rm -f $(OBJS)

clean:
	rm -f *.o ../../lib/libstreamgen.a

