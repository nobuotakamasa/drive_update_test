# Copyright (c) 2024, NVIDIA Corporation.  All rights reserved.
# Refactored: replaced nvdefs.mk dependency with ../common.mk

include ../common.mk

TARGET  := ffu_update
CFLAGS  := -fPIC $(NV_OPT) $(NV_CFLAGS) $(NV_DU_INC)
LDFLAGS := $(NV_SDK_LIB) $(NV_TARGET_LIB) $(NV_LDFLAGS)
LDLIBS  := -lnvmnand_private

OBJS := ffu_update.o

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

clobber clean:
	rm -f $(OBJS) $(TARGET) *.d
