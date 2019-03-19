
#include "v4l2_capture.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	const char *camera = "/dev/video0";
	v4l2_capture_t* capture;

    int image_to_capture = 30;
	if (argc > 1)
	{
		image_to_capture = atoi(argv[1]);
	}

	capture = v4l2_capture_open(camera, 640, 480, 30, PIXEL_FORMAT_MJPEG);

    while (image_to_capture-- > 0)
    {
        v4l2_capture(capture);
    }

	v4l2_capture_close(capture);

	return 0;
}

