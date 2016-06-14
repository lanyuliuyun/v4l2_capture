
#include "tinylib/linux/net/loop.h"
#include "tinylib/util/util.h"

#include "v4l2_capture.h"

#include <stdio.h>
#include <stdlib.h>

loop_t* g_loop = NULL;
int image_count = 30;

static
void image_sink(const captured_image_t* image, void* userdata)
{
    image_count--;
    if (image_count == 0)
    {
        loop_quit(g_loop);
    }

    return;
}

int main(int argc, char *argv[])
{
	const char *camera = "/dev/video0";
	v4l2_capture_t* capture;
    unsigned long long ts1, ts2;
    int image_to_capture = 30;

	if (argc > 1)
	{
		image_to_capture = atoi(argv[1]);
	}
    image_count = image_to_capture;
    
    g_loop = loop_new(64);

	capture = v4l2_capture_open(camera);
	v4l2_capture_start(capture, g_loop, image_sink, NULL);
    
    get_current_timestamp(&ts1);
    loop_loop(g_loop);
    get_current_timestamp(&ts2);
    
    printf("image count: %d, time elapsed: %d ms, fps: %f\n", image_to_capture, (int)(ts2 - ts1), (float)1000 * image_to_capture / (ts2-ts1));
    
	v4l2_capture_close(capture);
	
	return 0;
}

