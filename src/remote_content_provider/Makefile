# Copyright (c) 2020-2021, NVIDIA Corporation.  All rights reserved.
# Refactored: replaced nvdefs.mk dependency with ../common.mk

include ../common.mk

TARGET  := remote_content_provider
CFLAGS  := $(NV_OPT) $(NV_CFLAGS) $(NV_DU_INC)
LDFLAGS := $(NV_SDK_LIB) $(NV_TARGET_LIB) $(NV_LDFLAGS)
LDLIBS  := -lcurl -lnvdulink -lnvduplugin

OBJS := remote_content_provider.o

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

clobber clean:
	rm -f $(OBJS) $(TARGET) *.d
