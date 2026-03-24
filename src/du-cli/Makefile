# Copyright (c) 2024, NVIDIA Corporation.  All rights reserved.
# Refactored: replaced nvdefs.mk dependency with ../common.mk

include ../common.mk

TARGET  := du_cli
CFLAGS  := $(NV_OPT) $(NV_CFLAGS) $(NV_DU_INC)
LDFLAGS := $(NV_SDK_LIB) $(NV_TARGET_LIB) $(NV_LDFLAGS)
LDLIBS  := -lnvdusclient

OBJS := du_cli.o

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

clobber clean:
	rm -f $(OBJS) $(TARGET) *.d
