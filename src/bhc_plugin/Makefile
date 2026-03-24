# Copyright (c) 2020, NVIDIA Corporation.  All rights reserved.
# Refactored: replaced nvdefs.mk dependency with ../common.mk

include ../common.mk

TARGET  := libnvdubhc_api
SOURCES := dubhc_api.c
CFLAGS  := $(NV_OPT) $(NV_CFLAGS) $(NV_DU_INC)

ifeq ($(DU_BHC_FAILURE),1)
CFLAGS += -DDU_BHC_FAILURE
endif

OBJS := dubhc_api.o

all: $(TARGET).so

$(OBJS): $(SOURCES)
	$(CC) -c $(CFLAGS) $^

$(TARGET).so: $(OBJS)
	$(CROSSBIN)ld -shared --soname $(TARGET).so $^ -o $@

clobber clean:
	rm -f $(OBJS) $(TARGET).so
