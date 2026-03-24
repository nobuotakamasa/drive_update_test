# Copyright (c) 2021-2022, NVIDIA Corporation.  All rights reserved.
# Refactored: replaced nvdefs.mk dependency with ../common.mk

include ../common.mk

TARGET  := duinstaller
CFLAGS  := -fPIC $(NV_OPT) $(NV_CFLAGS) $(NV_DU_INC)
LDFLAGS := $(NV_SDK_LIB) $(NV_TARGET_LIB) $(NV_LDFLAGS)
LDLIBS  := -lnvdulink -lnvduplugin

OBJS := duinstaller.o \
        duinstaller_helpers.o \
        duinstaller_main.o \
        $(SRC_ROOT)/plugin_common/duinstaller_common.o

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

clobber clean:
	rm -f duinstaller.o duinstaller_helpers.o duinstaller_main.o \
	      $(TARGET) *.d
