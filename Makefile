
RASPB_PREFIX=/d/SysGCC/Raspberry/bin/arm-linux-gnueabihf-
CC = $(RASPB_PREFIX)gcc

capture: v4l2_capture.c capture.c
	$(CC) $^ -o $@ -g -Wall -Werror
