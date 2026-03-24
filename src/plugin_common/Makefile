# Builds plugin_common object files used by multiple targets.
# Called indirectly when parent targets reference these .o files.

include ../common.mk

CFLAGS := $(NV_OPT) $(NV_CFLAGS) $(NV_DU_INC)

OBJS := ducontent_provider_common.o duinstaller_common.o

all: $(OBJS)

%.o: %.c
	$(CC) -c $(CFLAGS) -o $@ $<

clobber clean:
	rm -f $(OBJS) *.d
