
#include "v4l2_capture.h"

#include <stdlib.h>
#include <string.h>

#include <math.h>
#include <time.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

#include <poll.h>
#include <sys/epoll.h>
#include <sys/time.h>

struct frame_buffer
{
	unsigned index;
	uint8_t *data;
	uint32_t len;
};

struct v4l2_capture
{
	int handle;

	int width;
	int height;
	int framerate;
	pixelformat_e format;

	struct{
		int support_stream_io;
        int support_rdwr_io;

		struct v4l2_format format;		
	}device;
	
	struct{
		unsigned count;
		struct frame_buffer *buffers;
	}buffer;
	
	int capure_started;
	
	captured_image_t frame;
    
  #ifdef USE_TINYLIB_EVENTLOOP
    void(*image_sink)(const captured_image_t* image, void* userdata);
    void* userdata;
    loop_t* loop;
    channel_t *channel;
  #endif
};

static 
int init_capture_format(v4l2_capture_t *capture, int width, int height, pixelformat_e format)
{
	unsigned image_size;
	struct v4l2_format image_format;
	
	memset(&capture->device.format, 0, sizeof(capture->device.format));
	capture->device.format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(capture->handle, VIDIOC_G_FMT, &capture->device.format) < 0)
	{
		return -1;
	}

	image_format = capture->device.format;

	image_format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	image_format.fmt.pix.width = width;
	image_format.fmt.pix.height = height;

	if (format == PIXEL_FORMAT_YUYV)
	{
		image_format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
	}
	else if (format == PIXEL_FORMAT_MJPEG)
	{
		image_format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	}

	if(ioctl(capture->handle, VIDIOC_S_FMT, &image_format) == 0)
	{
		image_format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if (ioctl(capture->handle, VIDIOC_G_FMT, &image_format) == 0)
		{
			capture->device.format = image_format;
		}
	}

	switch (capture->device.format.fmt.pix.pixelformat)
	{
		case V4L2_PIX_FMT_YUYV:
		{
			capture->frame.format.pixel_format = PIXEL_FORMAT_YUYV;
            image_size = capture->device.format.fmt.pix.bytesperline * capture->device.format.fmt.pix.height;
            if (capture->device.format.fmt.pix.sizeimage < image_size)
            {
                capture->device.format.fmt.pix.sizeimage = image_size;
            }
            
			break;
		}
		case V4L2_PIX_FMT_MJPEG:
		{
			capture->frame.format.pixel_format = PIXEL_FORMAT_MJPEG;
			break;
		}
		/* TODO: check other pixel format */
		default:
		{
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

/* 此处暂时只实现 MMAP 的 buffer 方式， 
 * userptr 的方式由于需要用户自己分配 buffer，请用户根据需要自行实现
 */
static 
int init_capture_buffer(v4l2_capture_t *capture)
{
	unsigned i, j;
	struct v4l2_requestbuffers req_buffer;
	struct v4l2_buffer buffer;

	if (capture->device.support_rdwr_io)
	{
        capture->frame.data = (uint8_t*)malloc(capture->device.format.fmt.pix.sizeimage);
		return 0;
	}

	do
	{
		memset(&req_buffer, 0, sizeof(req_buffer));
		req_buffer.count = 3;
		req_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		req_buffer.memory = V4L2_MEMORY_MMAP;
		if (ioctl(capture->handle, VIDIOC_REQBUFS, &req_buffer) != 0)
		{
			return -1;
		}

		if (req_buffer.count < 1)
		{
            return -1;
		}

		break;
	}while(1);

	capture->buffer.count = req_buffer.count;
	capture->buffer.buffers = (struct frame_buffer*)malloc(req_buffer.count * sizeof(struct frame_buffer));

	for (i = 0; i < req_buffer.count; ++i)
	{
		memset(&buffer, 0, sizeof(buffer));
		buffer.index = i;
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;
		if (ioctl(capture->handle, VIDIOC_QUERYBUF, &buffer) != 0)
		{
			free(capture->buffer.buffers);

			return -1;
		}

		capture->buffer.buffers[i].index = buffer.index;
		capture->buffer.buffers[i].len = buffer.length;
		capture->buffer.buffers[i].data = (uint8_t*)mmap(NULL, buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,
				capture->handle, buffer.m.offset);
		if (MAP_FAILED == capture->buffer.buffers[i].data)
		{
			for (j = 0; j < i; ++j)
			{
				munmap(capture->buffer.buffers[j].data, capture->buffer.buffers[j].len);
			}
			free(capture->buffer.buffers);

			return -1;
		}
	}

	for (i = 0; i < req_buffer.count; ++i)
	{
		memset(&buffer, 0, sizeof(buffer));
		buffer.index = i;
		buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffer.memory = V4L2_MEMORY_MMAP;
		if (ioctl(capture->handle, VIDIOC_QBUF, &buffer) != 0)
		{
			free(capture->buffer.buffers);

			return -1;
		}
	}
    
    if (capture->frame.format.bytesperline == 0)
    {
        if (capture->frame.format.pixel_format == PIXEL_FORMAT_YUYV)
        {
            capture->frame.format.bytesperline = capture->frame.format.width * 2;
        }
        else if (capture->frame.format.pixel_format == PIXEL_FORMAT_MJPEG)
        {
            capture->frame.format.bytesperline = capture->buffer.buffers[0].len;
        }
    }
    if (capture->frame.format.image_size == 0)
    {
        capture->frame.format.image_size = capture->buffer.buffers[0].len;
    }
    capture->frame.data = (uint8_t*)malloc(capture->device.format.fmt.pix.sizeimage);

	return 0;
}

v4l2_capture_t* v4l2_capture_open(const char *device, int width, int height, int framerate, pixelformat_e format)
{
	v4l2_capture_t *capture;
	struct v4l2_capability capablity;
    struct v4l2_streamparm stream_param;

	if (NULL == device)
	{
		return NULL;
	}

	capture = (v4l2_capture_t*)malloc(sizeof(*capture));

	capture->width = width;
	capture->height = height;
	capture->framerate = framerate;
	capture->format = format;

	do
	{
		capture->handle = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);
		if (capture->handle < 0)
		{
			break;
		}

		memset(&capablity, 0, sizeof(capablity));
		if (ioctl(capture->handle, VIDIOC_QUERYCAP, &capablity) < 0)
		{
			break;
		}

		if (0 == (V4L2_CAP_VIDEO_CAPTURE & capablity.capabilities))
		{
			break;
		}

		capture->device.support_stream_io = 0;
        capture->device.support_rdwr_io = 0;
		if (V4L2_CAP_STREAMING & capablity.capabilities)
		{
			capture->device.support_stream_io = 1;
		}
        else if (V4L2_CAP_READWRITE & capablity.capabilities)
        {
            capture->device.support_rdwr_io = 1;
        }
        else
        {
            break;
        }

		if (init_capture_format(capture, width, height, format) != 0)
		{
			break;
		}

        if (init_capture_buffer(capture) != 0)
        {
            break;
        }

        stream_param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(capture->handle, VIDIOC_G_PARM, &stream_param) == 0)
        {
            stream_param.parm.capture.timeperframe.numerator = 1;
            stream_param.parm.capture.timeperframe.denominator = framerate;
            ioctl(capture->handle, VIDIOC_S_PARM, &stream_param);

            capture->frame.format.fps = (int)ceilf((stream_param.parm.capture.timeperframe.denominator * 1.0) / stream_param.parm.capture.timeperframe.numerator);
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
    struct v4l2_buffer buffer;
    struct v4l2_requestbuffers req_buffer;
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
            buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buffer.memory = V4L2_MEMORY_MMAP;
            ioctl(capture->handle, VIDIOC_DQBUF, &buffer);
        }

		for (i = 0; i < capture->buffer.count; ++i)
		{
			munmap(capture->buffer.buffers[i].data, capture->buffer.buffers[i].len);
		}

        {
            req_buffer.count = 0;
            req_buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            req_buffer.memory = V4L2_MEMORY_MMAP;
            ioctl(capture->handle, VIDIOC_REQBUFS, &req_buffer);
        }

		free(capture->buffer.buffers);
        free(capture->frame.data);
	}
    else /* if (capture->device.support_rdwr_io) */
    {
        free(capture->frame.data);
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
		return NULL;
	}

	waittime = 100;
	if (capture->capure_started == 0)
	{
		if (capture->device.support_stream_io)
		{
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if (ioctl(capture->handle, VIDIOC_STREAMON, &type) != 0)
			{
				return NULL;
			}
		}

		capture->capure_started = 1;
		waittime = 2000;
	}

	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = capture->handle;
	pfd.events = POLLIN;

	ret = poll(&pfd, 1, waittime);
	if (ret <= 0)
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
			return NULL;
		}

        capture->frame.timestamp = buffer.timestamp.tv_sec * 1000 + buffer.timestamp.tv_usec / 1000;
		memcpy(capture->frame.data, capture->buffer.buffers[buffer.index].data, buffer.bytesused);
		capture->frame.len = buffer.bytesused;
		ioctl(capture->handle, VIDIOC_QBUF, &buffer);
	}
	else
	{
		struct timespec ts_spec;
		ret = read(capture->handle, capture->frame.data, capture->device.format.fmt.pix.sizeimage);
		if (ret <= 0)
		{
			return NULL;
		}

		clock_gettime(CLOCK_MONOTONIC, &ts_spec);
		capture->frame.timestamp = ts_spec.tv_sec + ts_spec.tv_nsec / 1000000;
		capture->frame.len = ret;
	}

	return &capture->frame;
}

#ifdef USE_TINYLIB_EVENTLOOP

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
                break;
            }

			capture->frame.timestamp = buffer.timestamp.tv_sec * 1000 + buffer.timestamp.tv_usec / 1000;
            capture->frame.data = capture->buffer.buffers[buffer.index].data;
            capture->frame.len = capture->buffer.buffers[buffer.index].len;
            capture->image_sink(&capture->frame, capture->userdata);

            if (ioctl(capture->handle, VIDIOC_QBUF, &buffer) != 0)
            {
                fprintf(stderr, "capture_onevent: ioctl(VIDIOC_QBUF)\n");
            }
        }
        else
        {
			struct timespec ts_spec;

            ret = read(capture->handle, capture->frame.data, capture->device.format.fmt.pix.sizeimage);
            if (ret <= 0)
            {
                break;
            }

			clock_gettime(CLOCK_MONOTONIC, &ts_spec);
			capture->frame.timestamp = ts_spec.tv_sec + ts_spec.tv_nsec / 1000000;

            capture->frame.len = ret;
            capture->image_sink(&capture->frame, capture->userdata);
        }
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

    if (capture->device.support_stream_io)
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

#endif

