
RASPB_PREFIX=/d/SysGCC/Raspberry/bin/arm-linux-gnueabihf-
CC = $(RASPB_PREFIX)gcc

camera: camera.c
	$(CC) $^ -o $@ -g -Wall
	
capture: v4l2_capture.c capture.c
	$(CC) $^ -o $@ -g -Wall -Werror
