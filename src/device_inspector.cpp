/* ---------------------------------------------------------------------------
** Simple V4L2 device inspector utility
** Compile: g++ -o device_inspector device_inspector.cpp
** -------------------------------------------------------------------------*/

#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <cstring>
#include <cstdio>

#ifdef __linux__
#include <linux/videodev2.h>
#else
// Fallback definitions for non-Linux systems
#define V4L2_PIX_FMT_H264     v4l2_fourcc('H', '2', '6', '4')
#define V4L2_PIX_FMT_MJPEG    v4l2_fourcc('M', 'J', 'P', 'G')
#define V4L2_PIX_FMT_YUYV     v4l2_fourcc('Y', 'U', 'Y', 'V')
#define V4L2_CAP_VIDEO_CAPTURE 0x00000001
#define V4L2_CAP_STREAMING     0x04000000
#define V4L2_BUF_TYPE_VIDEO_CAPTURE 1

struct v4l2_capability {
    char driver[16];
    char card[32];
    char bus_info[32];
    unsigned int capabilities;
    unsigned int device_caps;
    unsigned int reserved[3];
};

struct v4l2_fmtdesc {
    unsigned int index;
    unsigned int type;
    unsigned int flags;
    char description[32];
    unsigned int pixelformat;
    unsigned int reserved[4];
};

#define VIDIOC_QUERYCAP    0x80685600
#define VIDIOC_ENUM_FMT    0xc0405602
#define v4l2_fourcc(a, b, c, d) (((unsigned int)(a) << 0) | ((unsigned int)(b) << 8) | ((unsigned int)(c) << 16) | ((unsigned int)(d) << 24))
#endif

std::string fourccToString(unsigned int fourcc) {
    char str[5];
    str[0] = fourcc & 0xff;
    str[1] = (fourcc >> 8) & 0xff;
    str[2] = (fourcc >> 16) & 0xff;
    str[3] = (fourcc >> 24) & 0xff;
    str[4] = '\0';
    return std::string(str);
}

void inspectDevice(const std::string& devicePath) {
    std::cout << "\n=== Inspecting " << devicePath << " ===" << std::endl;
    
    int fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        std::cout << "Cannot open device: " << strerror(errno) << std::endl;
        return;
    }
    
#ifdef __linux__
    // Get device capabilities
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
        std::cout << "Driver: " << cap.driver << std::endl;
        std::cout << "Card: " << cap.card << std::endl;
        std::cout << "Bus: " << cap.bus_info << std::endl;
        std::cout << "Capabilities: 0x" << std::hex << cap.capabilities << std::dec << std::endl;
        
        if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
            std::cout << "  - Video Capture: YES" << std::endl;
        }
        if (cap.capabilities & V4L2_CAP_STREAMING) {
            std::cout << "  - Streaming: YES" << std::endl;
        }
    } else {
        std::cout << "Cannot query capabilities: " << strerror(errno) << std::endl;
    }
    
    // Enumerate formats
    std::cout << "\nSupported Formats:" << std::endl;
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    bool hasH264 = false;
    bool hasMJPEG = false;
    
    for (fmtdesc.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0; fmtdesc.index++) {
        std::string fourcc = fourccToString(fmtdesc.pixelformat);
        std::cout << "  " << fourcc << " - " << fmtdesc.description << std::endl;
        
        if (fmtdesc.pixelformat == V4L2_PIX_FMT_H264) {
            hasH264 = true;
        }
        if (fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG) {
            hasMJPEG = true;
        }
    }
    
    std::cout << "\nSnapshot Compatibility:" << std::endl;
    if (hasH264 && hasMJPEG) {
        std::cout << "  ✓ Device supports both H264 and MJPEG - dual format possible" << std::endl;
    } else if (hasH264) {
        std::cout << "  ⚠ Device supports H264 but not MJPEG - format switching needed" << std::endl;
    } else if (hasMJPEG) {
        std::cout << "  ⚠ Device supports MJPEG but not H264 - not suitable for H264 streaming" << std::endl;
    } else {
        std::cout << "  ✗ Device doesn't support H264 or MJPEG" << std::endl;
    }
#else
    std::cout << "V4L2 inspection not available on this platform" << std::endl;
#endif
    
    close(fd);
}

void testDualAccess(const std::string& devicePath) {
    std::cout << "\n=== Testing Dual Access for " << devicePath << " ===" << std::endl;
    
#ifdef __linux__
    // Test if we can open the device twice simultaneously
    int fd1 = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
    if (fd1 < 0) {
        std::cout << "Cannot open device for first access: " << strerror(errno) << std::endl;
        return;
    }
    
    int fd2 = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
    if (fd2 < 0) {
        std::cout << "Cannot open device for second access: " << strerror(errno) << std::endl;
        std::cout << "  ✗ Device does not support multiple simultaneous connections" << std::endl;
    } else {
        std::cout << "  ✓ Device supports multiple simultaneous connections" << std::endl;
        close(fd2);
    }
    
    close(fd1);
#else
    std::cout << "Dual access testing not available on this platform" << std::endl;
#endif
}

void findRelatedDevices(const std::string& baseDevice) {
    std::cout << "\n=== Finding Related Devices ===" << std::endl;
    
    // Extract base device number
    std::string baseNum = baseDevice.substr(baseDevice.find_last_not_of("0123456789") + 1);
    int baseDeviceNum = std::stoi(baseNum);
    
    std::cout << "Base device: " << baseDevice << " (number: " << baseDeviceNum << ")" << std::endl;
    
    for (int offset = 1; offset <= 3; offset++) {
        std::string candidatePath = "/dev/video" + std::to_string(baseDeviceNum + offset);
        
        if (access(candidatePath.c_str(), F_OK) == 0) {
            std::cout << "Found related device: " << candidatePath << std::endl;
            inspectDevice(candidatePath);
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "V4L2 Device Inspector" << std::endl;
    std::cout << "====================" << std::endl;
    
    if (argc > 1) {
        std::string devicePath = argv[1];
        inspectDevice(devicePath);
        testDualAccess(devicePath);
        findRelatedDevices(devicePath);
    } else {
        std::cout << "\nScanning for available devices..." << std::endl;
        
        // Check for common video devices
        for (int i = 0; i < 8; i++) {
            std::string devicePath = "/dev/video" + std::to_string(i);
            
            if (access(devicePath.c_str(), F_OK) == 0) {
                inspectDevice(devicePath);
            }
        }
        
        std::cout << "\nUsage: " << argv[0] << " [device_path]" << std::endl;
        std::cout << "Example: " << argv[0] << " /dev/video0" << std::endl;
    }
    
    return 0;
} 