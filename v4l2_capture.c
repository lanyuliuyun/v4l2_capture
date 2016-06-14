
#include "v4l2_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <poll.h>
#include <sys/epoll.h>
#include <linux/videodev2.h>

struct frame_buffer
{
	unsigned index;
	uint8_t *data;
	uint32_t len;
};

struct v4l2_capture
{
	int handle;
	struct{
		int support_stream_io;

		struct v4l2_format format;		
	}device;
	
	struct{
		unsigned count;
		struct frame_buffer *buffers;
	}buffer;
	
	int capure_started;
	
	captured_image_t frame;
    
    void(*image_sink)(const captured_image_t* image, void* userdata);
    void* userdata;
    loop_t* loop;
    channel_t *channel;
};

#define DEFAULT_IMAGE_WIDTH (640)
#define DEFAULT_IMAGE_HEIGHT (480)

static 
int init_capture_format(v4l2_capture_t *capture)
{
	unsigned image_size;
	struct v4l2_format image_format;
	
	memset(&capture->device.format, 0, sizeof(capture->device.format));
	capture->device.format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(capture->handle, VIDIOC_G_FMT, &capture->device.format) < 0)
	{
		printf("init_capture_format: ioctl(VIDIOC_G_FMT) failed\n");
		return -1;
	}

	image_format = capture->device.format;
	image_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUV422P;
	image_format.fmt.pix.width = DEFAULT_IMAGE_WIDTH;
	image_format.fmt.pix.width = DEFAULT_IMAGE_HEIGHT;
	if(ioctl(capture->handle, VIDIOC_S_FMT, &image_format) == 0)
	{
		image_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (ioctl(capture->handle, VIDIOC_G_FMT, &image_format) == 0)
		{
			capture->device.format = image_format;
		}
	}

	image_size = capture->device.format.fmt.pix.bytesperline * capture->device.format.fmt.pix.height;
	if (capture->device.format.fmt.pix.sizeimage < image_size)
	{
		capture->device.format.fmt.pix.sizeimage = image_size;
	}

	switch (capture->device.format.fmt.pix.pixelformat)
	{
		case V4L2_PIX_FMT_YUYV:
		{
			capture->frame.format.pixel_format = PIXEL_FORMAT_YUYV;
			break;
		}
		/* TODO: check other pixel format */
		default:
		{
			printf("init_capture_format: unsupported pixel format: 0x%x\n", capture->device.format.fmt.pix.pixelformat);
			return -1;
		}
	}

	capture->frame.format.width = capture->device.format.fmt.pix.width;
	capture->frame.format.height = capture->device.format.fmt.pix.height;
	capture->frame.format.bytesperline = capture->device.format.fmt.pix.bytesperline;	
	capture->frame.format.image_size = capture->device.format.fmt.pix.sizeimage;
	capture->frame.data = NULL;
	capture->frame.len = capture->device.format.fmt.pix.sizeimage;

	return 0;
}

static 
int init_capture_buffer(v4l2_capture_t *capture)
{
	unsigned i, j;
	struct v4l2_requestbuffers req_buffer;
	struct v4l2_buffer buffer;
	uint32_t max_frame_buffer;

	if (0 == capture->device.support_stream_io)
	{
        capture->frame.data = (uint8_t*)malloc(capture->device.format.fmt.pix.sizeimage);
		return 0;
	}
	
	max_frame_buffer = 16;
	do
	{
		memset(&req_buffer, 0, sizeof(req_buffer));
		req_buffer.count = max_frame_buffer;
		req_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		req_buffer.memory = V4L2_MEMORY_MMAP;
		if (ioctl(capture->handle, VIDIOC_REQBUFS, &req_buffer) != 0)
		{
			printf("init_capture_buffer: ioctl(VIDIOC_REQBUFS) failed\n");
			return -1;
		}

		if (req_buffer.count < max_frame_buffer)
		{
			if (1 == max_frame_buffer)
			{
				fprintf(stderr, "init_capture_buffer: can NOT request buffer\n");
				return -1;
			}

			max_frame_buffer--;			
			continue;
		}

		break;
	}while(1);
	
	capture->buffer.count = max_frame_buffer;
	capture->buffer.buffers = (struct frame_buffer*)calloc(1, max_frame_buffer * sizeof(struct frame_buffer));
	if (NULL == capture->buffer.buffers)
	{
		printf("init_capture_buffer: calloc() failed\n");
		return -1;
	}

	for (i = 0; i < max_frame_buffer; ++i)
	{
		memset(&buffer, 0, sizeof(buffer));
		buffer.index = i;
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;
		if (ioctl(capture->handle, VIDIOC_QUERYBUF, &buffer) != 0)
		{
			printf("init_capture_buffer: ioctl(VIDIOC_QUERYBUF) failed\n");
			free(capture->buffer.buffers);

			return -1;
		}

		capture->buffer.buffers[i].index = buffer.index;
		capture->buffer.buffers[i].len = buffer.length;
		capture->buffer.buffers[i].data = (uint8_t*)mmap(NULL, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,
				capture->handle, buffer.m.offset);
		if (MAP_FAILED == capture->buffer.buffers[i].data)
		{
			printf("init_capture_buffer: mmap() failed\n");
			for (j = 0; j < i; ++j)
			{
				munmap(capture->buffer.buffers[j].data, capture->buffer.buffers[j].len);
			}
			free(capture->buffer.buffers);

			return -1;
		}
	}
	
	for (i = 0; i < max_frame_buffer; ++i)
	{
		memset(&buffer, 0, sizeof(buffer));
		buffer.index = i;
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;

		if (ioctl(capture->handle, VIDIOC_QBUF, &buffer) != 0)
		{
			printf("init_capture_buffer: ioctl(VIDIOC_QBUF) failed\n");
			free(capture->buffer.buffers);
			
			return -1;
		}
	}

	return 0;
}

v4l2_capture_t* v4l2_capture_open(const char *device)
{
	v4l2_capture_t *capture;
	struct v4l2_capability capablity;
	
	if (NULL == device)
	{
		printf("v4l2_capture_open: null device path\n");
		return NULL;
	}

	capture = (v4l2_capture_t*)calloc(1, sizeof(*capture));
	if (NULL == capture)
	{
		printf("v4l2_capture_open: malloc() failed\n");
		return NULL;
	}

	do
	{
		capture->handle = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);
		if (capture->handle < 0)
		{
			printf("v4l2_capture_open: open(%s) failed\n", device);
			break;
		}

		memset(&capablity, 0, sizeof(capablity));
		if (ioctl(capture->handle, VIDIOC_QUERYCAP, &capablity) < 0)
		{
			printf("v4l2_capture_open: ioctl(VIDIOC_QUERYCAP) failed\n");
			break;
		}

		if (0 == (V4L2_CAP_VIDEO_CAPTURE & capablity.capabilities))
		{
			printf("v4l2_capture_open: '%s' can NOT capture video\n", device);
			break;
		}
		
		capture->device.support_stream_io = 0;
		if (V4L2_CAP_STREAMING & capablity.capabilities)
		{
			capture->device.support_stream_io = 1;
		}
		
		if(init_capture_format(capture) != 0 || init_capture_buffer(capture) != 0)
		{
			break;
		}

		capture->capure_started = 0;
		
		return capture;
	}while(0);

	if (capture->handle >= 0)
	{
		close(capture->handle);
	}
	free(capture);

	return NULL;
}

void v4l2_capture_close(v4l2_capture_t *capture)
{
	unsigned i;
	int type;

	if (NULL == capture)
	{
		return;
	}
	
	if (capture->device.support_stream_io)
	{
		if(capture->capure_started)
		{
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			ioctl(capture->handle, VIDIOC_STREAMOFF, &type);
            capture->capure_started = 0;
		}

		for (i = 0; i < capture->buffer.count; ++i)
		{
			munmap(capture->buffer.buffers[i].data, capture->buffer.buffers[i].len);
		}

		free(capture->buffer.buffers);
	}

	close(capture->handle);
	free(capture);

	return;
}

const imageformat_t* v4l2_capture_get_imageformat(v4l2_capture_t *capture)
{
	if (NULL == capture)
	{
		return NULL;
	}

	return &capture->frame.format;
}

const captured_image_t* v4l2_capture(v4l2_capture_t *capture)
{
	int type;
	int ret;
	int waittime;
	struct pollfd pfd;
	
	struct v4l2_buffer buffer;

	if (NULL == capture)
	{
		printf("v4l2_capture: bad capture\n");
		return NULL;
	}

	waittime = 10;
	if (capture->capure_started == 0)
	{
		if (capture->device.support_stream_io)
		{
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if (ioctl(capture->handle, VIDIOC_STREAMON, &type) != 0)
			{
				printf("v4l2_capture: ioctl() failed");
				return NULL;
			}
		}

		capture->capure_started = 1;
		waittime = 2000;
	}
	
	/* TODO: 
	 * we may register the device handle to a event loop
	 * and use callback to send the captured image back to the user instead of polling it.
     */

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = capture->handle;
	pfd.events = POLLIN;

	ret = poll(&pfd, 1, waittime);
	if (ret < 0)
	{
		printf("v4l2_capture: poll() failed");
		return NULL;
	}

	if (0 == ret)
	{
		return NULL;
	}

	if (capture->device.support_stream_io)
	{
		memset(&buffer, 0, sizeof(buffer));
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;

		if (ioctl(capture->handle, VIDIOC_DQBUF, &buffer) != 0)
		{
			printf("v4l2_capture: ioctl(VIDIOC_DQBUF) failed");
			return NULL;
		}
		
        capture->frame.data = capture->buffer.buffers[buffer.index].data;
		capture->frame.len = capture->buffer.buffers[buffer.index].len;
		if (ioctl(capture->handle, VIDIOC_QBUF, &buffer) != 0)
		{
			printf ("v4l2_capture: ioctl(VIDIOC_QBUF)");
		}
	}
	else
	{
		ret = read(capture->handle, capture->frame.data, capture->device.format.fmt.pix.sizeimage);
		if (ret <= 0)
		{
			printf("v4l2_capture: read() error");
			return NULL;
		}

		capture->frame.len = ret;
	}

	return &capture->frame;
}

static
void capture_onevent(int fd, int event, void* userdata)
{
    v4l2_capture_t *capture = (v4l2_capture_t*)userdata;
    struct v4l2_buffer buffer;
    int ret;
    
    if ((event & EPOLLIN) == 0)
    {
        return;
    }
    
    do
    {
        if (capture->device.support_stream_io)
        {
            memset(&buffer, 0, sizeof(buffer));
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;

            if (ioctl(capture->handle, VIDIOC_DQBUF, &buffer) != 0)
            {
                fprintf(stderr, "capture_onevent: ioctl(VIDIOC_DQBUF) failed\n");
                break;
            }
            
            capture->frame.data = capture->buffer.buffers[buffer.index].data;
            capture->frame.len = capture->buffer.buffers[buffer.index].len;

            if (ioctl(capture->handle, VIDIOC_QBUF, &buffer) != 0)
            {
                fprintf(stderr, "capture_onevent: ioctl(VIDIOC_QBUF)\n");
            }
        }
        else
        {
            ret = read(capture->handle, capture->frame.data, capture->device.format.fmt.pix.sizeimage);
            if (ret <= 0)
            {
                fprintf(stderr, "capture_onevent: read() error\n");
                break;
            }

            capture->frame.len = ret;
        }
        capture->image_sink(&capture->frame, capture->userdata);
    } while(1);

    return;
}

int v4l2_capture_start(v4l2_capture_t *capture, loop_t* loop, void(*image_sink)(const captured_image_t* image, void* userdata), void* userdata)
{
    int type;
    
    if (NULL == capture || NULL == loop || NULL == image_sink)
    {
        return -1;
    }

	if (capture->capure_started == 0)
	{
		if (capture->device.support_stream_io)
		{
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if (ioctl(capture->handle, VIDIOC_STREAMON, &type) != 0)
			{
				printf("v4l2_capture_start: ioctl() failed");
				return -1;
			}
		}

        capture->image_sink = image_sink;
        capture->userdata = userdata;
        capture->loop = loop;
        capture->channel = channel_new(capture->handle, loop, capture_onevent, capture);
        channel_setevent(capture->channel, EPOLLIN);

        capture->capure_started = 1;
    }
    
    return 0;
}


void v4l2_capture_stop(v4l2_capture_t *capture)
{
    int type;
    
    if (NULL == capture)
    {
        return;
    }
    
    if (capture->capure_started == 0)
    {
        return;
    }
    
    if (0 == capture->device.support_stream_io)
    {
        free(capture->frame.data);
    }
    else
    {
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(capture->handle, VIDIOC_STREAMOFF, &type);
    }
    
    channel_detach(capture->channel);
    channel_destroy(capture->channel);
    capture->channel = NULL;
    capture->loop = NULL;
    capture->image_sink = NULL;

    capture->capure_started = 0;

    return;
}
