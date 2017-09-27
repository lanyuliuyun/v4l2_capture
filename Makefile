
TOOLCHAIN_PREFIX=
CC = $(RASPB_PREFIX)gcc

TINYLIB_ROOT = $(HOME)/opensource/tinylib
CPP_FLAGS = -I$(TINYLIB_ROOT)
CC_FLAGS = -Wall -Werror -fno-omit-frame-pointer -O2
LD_FLAGS = -L$(TINYLIB_ROOT)/output -ltinylib -lpthread

capture: v4l2_capture.c capture.c
	$(CC) $^ -o $@ $(CPP_FLAGS) $(CC_FLAGS) $(LD_FLAGS)
