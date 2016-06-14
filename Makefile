
RASPB_PREFIX=/d/SysGCC/Raspberry/bin/arm-linux-gnueabihf-
CC = $(RASPB_PREFIX)gcc

TINYLIB = /e/tinylib
CPP_FLAGS = -I$(TINYLIB)
CC_FLAGS = -Wall -Werror -fno-omit-frame-pointer -O2
LD_FLAGS = -L$(TINYLIB)/output -ltinylib.raspi -lpthread

capture: v4l2_capture.c capture.c
	$(CC) $^ -o $@ $(CPP_FLAGS) $(CC_FLAGS) $(LD_FLAGS)
