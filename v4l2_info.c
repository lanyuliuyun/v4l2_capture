
#include <linux/videodev2.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    const char *device = argv[1];

    int device_fd = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC, 0);
    if (device_fd < 0)
    {
        return -1;
    }

    printf("device: %s, detail:\n", device);
    do
    {
        struct v4l2_capability capablity;
        ioctl(device_fd, VIDIOC_QUERYCAP, &capablity);
        if ((V4L2_CAP_VIDEO_CAPTURE & capablity.capabilities) == 0)
        {
            break;
        }
        printf(" support capture\n");
        printf(" support pixel format:");
        for (int fmt_idx = 0; ;fmt_idx++)
        {
            struct v4l2_fmtdesc fmt_desc;
            memset(&fmt_desc, 0, sizeof(fmt_desc));
            fmt_desc.index = fmt_idx;
            fmt_desc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if (ioctl(device_fd, VIDIOC_ENUM_FMT, &fmt_desc) != 0)
            {
                break;
            }

            printf(" pix: 0x%04X, description: %s\n", fmt_desc.pixelformat, fmt_desc.description);
            for (int j = 0; ; ++j)
            {
                struct v4l2_frmsizeenum frame_size;
                memset(&frame_size, 0, sizeof(&frame_size));
                frame_size.index = j;
                frame_size.pixel_format = fmt_desc.pixelformat;
                frame_size.type = V4L2_FRMSIZE_TYPE_DISCRETE;
                if (ioctl(device_fd, VIDIOC_ENUM_FRAMESIZES, &frame_size) != 0)
                {
                    break;
                }

                printf("  frame size: %dx%d\n", frame_size.discrete.width, frame_size.discrete.height);
                
                for (int k = 0; ; ++k)
                {
                    struct v4l2_frmivalenum interval;
                    memset(&interval, 0, sizeof(interval));
                    interval.index = k;
                    interval.pixel_format = fmt_desc.pixelformat;
                    interval.width = frame_size.discrete.width;
                    interval.height = frame_size.discrete.height;
                    interval.type = V4L2_FRMIVAL_TYPE_DISCRETE;
                    if (ioctl(device_fd, VIDIOC_ENUM_FRAMEINTERVALS, &interval) != 0)
                    {
                        break;
                    }
                    printf("   fps: %d/%d\n", interval.discrete.denominator, interval.discrete.numerator);
                }
            }
        }
    } while(0);

    close(device_fd);

    return 0;
}
