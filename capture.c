
#include "v4l2_capture.h"

#include <stdio.h>

int main(int argc, char *argv[])
{
	const char *camera;
	v4l2_capture_t* capture;
	const captured_image_t* image;
	
	FILE *f;
	
	if (argc < 2)
	{
		camera = "/dev/video0";
	}
	else
	{
		camera = argv[1];
	}
	
	f = fopen("frame.yuv", "w");
	
	capture = v4l2_capture_open(camera);
	
	image = v4l2_capture(capture);
	printf(
		"captured a frame:\n"
		"\tpixel format: %d\n"
		"\timage width: %u\n"
		"\timage height: %u\n"
		"\tstride: %u\n"
		"\timage size: %u\n",
		image->format.pixel_format,
		image->format.width,
		image->format.height,
		image->format.bytesperline,
		image->len
	);
	
	fwrite(image->data, 1, image->len, f);
	fclose(f);
	
	v4l2_capture_close(capture);
	
	return 0;
}

