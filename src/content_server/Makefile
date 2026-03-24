# Copyright (c) 2021, NVIDIA Corporation.  All rights reserved.
# Refactored: replaced nvdefs.mk dependency with ../common.mk

include ../common.mk

TARGET  := content_server
CFLAGS  := $(NV_OPT) $(NV_CFLAGS) $(NV_DU_INC)
LDFLAGS := $(NV_SDK_LIB) $(NV_TARGET_LIB) $(NV_LDFLAGS)
LDLIBS  := -lnvdulink -lnvduplugin -lpthread

OBJS := content_server.o \
        $(SRC_ROOT)/plugin_common/ducontent_provider_common.o

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

clobber clean:
	rm -f content_server.o $(TARGET) *.d
