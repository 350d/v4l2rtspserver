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
#include <map>
#include <thread>
#include <chrono>

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

struct v4l2_format {
    unsigned int type;
    union {
        struct {
            unsigned int width;
            unsigned int height;
            unsigned int pixelformat;
            unsigned int field;
            unsigned int bytesperline;
            unsigned int sizeimage;
            unsigned int colorspace;
            unsigned int priv;
        } pix;
        unsigned char raw_data[200];
    } fmt;
};

#define VIDIOC_QUERYCAP    0x80685600
#define VIDIOC_ENUM_FMT    0xc0405602
#define VIDIOC_S_FMT       0xc0cc5605
#define VIDIOC_G_FMT       0xc0cc5604
#define v4l2_fourcc(a, b, c, d) (((unsigned int)(a) << 0) | ((unsigned int)(b) << 8) | ((unsigned int)(c) << 16) | ((unsigned int)(d) << 24))
#endif

struct DeviceInfo {
    std::string path;
    std::string driver;
    std::string card;
    std::vector<unsigned int> formats;
    bool hasH264;
    bool hasMJPEG;
    bool supportsStreaming;
    bool supportsDualAccess;
};

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

bool testFormatConfiguration(const std::string& devicePath, unsigned int pixelformat, int width = 640, int height = 480) {
#ifdef __linux__
    int fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width;
    fmt.fmt.pix.height = height;
    fmt.fmt.pix.pixelformat = pixelformat;
    fmt.fmt.pix.field = 0; // V4L2_FIELD_NONE
    
    bool success = (ioctl(fd, VIDIOC_S_FMT, &fmt) == 0);
    close(fd);
    return success;
#else
    return false;
#endif
}

void testMultiDeviceFormats(const std::vector<std::string>& devices) {
    std::cout << "\n=== Multi-Device Format Configuration Test ===" << std::endl;
    
    if (devices.size() < 2) {
        std::cout << "Need at least 2 related devices for multi-device testing" << std::endl;
        return;
    }
    
    std::cout << "Testing concurrent format configuration..." << std::endl;
    
    // Test H264 on first device, MJPEG on second
    std::string dev1 = devices[0];
    std::string dev2 = devices[1];
    
    std::cout << "Testing: " << dev1 << " (H264) + " << dev2 << " (MJPEG)" << std::endl;
    
    bool h264Success = testFormatConfiguration(dev1, V4L2_PIX_FMT_H264, 1920, 1080);
    bool mjpegSuccess = testFormatConfiguration(dev2, V4L2_PIX_FMT_MJPEG, 640, 480);
    
    if (h264Success && mjpegSuccess) {
        std::cout << "  ✓ Concurrent H264+MJPEG configuration successful" << std::endl;
        std::cout << "  → Ideal for H264 streaming + MJPEG snapshots" << std::endl;
    } else {
        std::cout << "  ⚠ Concurrent format configuration failed:" << std::endl;
        if (!h264Success) std::cout << "    - H264 setup failed on " << dev1 << std::endl;
        if (!mjpegSuccess) std::cout << "    - MJPEG setup failed on " << dev2 << std::endl;
    }
    
    // Test reverse configuration
    std::cout << "Testing: " << dev1 << " (MJPEG) + " << dev2 << " (H264)" << std::endl;
    
    bool mjpegSuccess2 = testFormatConfiguration(dev1, V4L2_PIX_FMT_MJPEG, 640, 480);
    bool h264Success2 = testFormatConfiguration(dev2, V4L2_PIX_FMT_H264, 1920, 1080);
    
    if (h264Success2 && mjpegSuccess2) {
        std::cout << "  ✓ Reverse concurrent H264+MJPEG configuration successful" << std::endl;
    } else {
        std::cout << "  ⚠ Reverse concurrent format configuration failed" << std::endl;
    }
}

void testConcurrentAccess(const std::vector<std::string>& devices) {
    std::cout << "\n=== Concurrent Access Stress Test ===" << std::endl;
    
    if (devices.empty()) {
        std::cout << "No devices provided for testing" << std::endl;
        return;
    }
    
    std::vector<int> fds;
    std::vector<bool> openSuccess(devices.size(), false);
    
    // Try to open all devices simultaneously
    for (size_t i = 0; i < devices.size(); i++) {
        int fd = open(devices[i].c_str(), O_RDWR | O_NONBLOCK);
        if (fd >= 0) {
            fds.push_back(fd);
            openSuccess[i] = true;
            std::cout << "  ✓ " << devices[i] << " opened successfully" << std::endl;
        } else {
            fds.push_back(-1);
            std::cout << "  ✗ " << devices[i] << " failed to open: " << strerror(errno) << std::endl;
        }
    }
    
    // Test if we can configure formats while all are open
    if (fds.size() >= 2 && fds[0] >= 0 && fds[1] >= 0) {
        std::cout << "\nTesting format configuration while devices are open..." << std::endl;
        
#ifdef __linux__
        struct v4l2_format fmt1, fmt2;
        memset(&fmt1, 0, sizeof(fmt1));
        memset(&fmt2, 0, sizeof(fmt2));
        
        fmt1.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt1.fmt.pix.width = 1920;
        fmt1.fmt.pix.height = 1080;
        fmt1.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
        
        fmt2.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt2.fmt.pix.width = 640;
        fmt2.fmt.pix.height = 480;
        fmt2.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        
        bool fmt1Success = (ioctl(fds[0], VIDIOC_S_FMT, &fmt1) == 0);
        bool fmt2Success = (ioctl(fds[1], VIDIOC_S_FMT, &fmt2) == 0);
        
        if (fmt1Success && fmt2Success) {
            std::cout << "  ✓ Concurrent format configuration successful" << std::endl;
            std::cout << "  → " << devices[0] << ": H264 1920x1080" << std::endl;
            std::cout << "  → " << devices[1] << ": MJPEG 640x480" << std::endl;
        } else {
            std::cout << "  ⚠ Concurrent format configuration partially failed" << std::endl;
        }
#endif
    }
    
    // Close all file descriptors
    for (int fd : fds) {
        if (fd >= 0) {
            close(fd);
        }
    }
    
    // Summary
    int successCount = 0;
    for (bool success : openSuccess) {
        if (success) successCount++;
    }
    
    std::cout << "\nConcurrent Access Summary:" << std::endl;
    std::cout << "  Devices tested: " << devices.size() << std::endl;
    std::cout << "  Successful opens: " << successCount << std::endl;
    std::cout << "  Multi-device capability: " << (successCount > 1 ? "YES" : "NO") << std::endl;
}

std::vector<DeviceInfo> analyzeDeviceGroup(const std::vector<std::string>& devices) {
    std::cout << "\n=== Device Group Analysis ===" << std::endl;
    
    std::vector<DeviceInfo> deviceInfos;
    
    for (const std::string& devicePath : devices) {
        DeviceInfo info;
        info.path = devicePath;
        info.hasH264 = false;
        info.hasMJPEG = false;
        info.supportsStreaming = false;
        info.supportsDualAccess = false;
        
        int fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            std::cout << devicePath << ": Cannot open" << std::endl;
            continue;
        }
        
#ifdef __linux__
        // Get capabilities
        struct v4l2_capability cap;
        memset(&cap, 0, sizeof(cap));
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            info.driver = std::string(reinterpret_cast<char*>(cap.driver));
            info.card = std::string(reinterpret_cast<char*>(cap.card));
            info.supportsStreaming = (cap.capabilities & V4L2_CAP_STREAMING) != 0;
        }
        
        // Get formats
        struct v4l2_fmtdesc fmtdesc;
        memset(&fmtdesc, 0, sizeof(fmtdesc));
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        
        for (fmtdesc.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0; fmtdesc.index++) {
            info.formats.push_back(fmtdesc.pixelformat);
            if (fmtdesc.pixelformat == V4L2_PIX_FMT_H264) {
                info.hasH264 = true;
            }
            if (fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG) {
                info.hasMJPEG = true;
            }
        }
#endif
        
        close(fd);
        
        // Test dual access
        int fd1 = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
        if (fd1 >= 0) {
            int fd2 = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
            if (fd2 >= 0) {
                info.supportsDualAccess = true;
                close(fd2);
            }
            close(fd1);
        }
        
        deviceInfos.push_back(info);
        
        std::cout << devicePath << ":" << std::endl;
        std::cout << "  Driver: " << info.driver << std::endl;
        std::cout << "  Card: " << info.card << std::endl;
        std::cout << "  H264: " << (info.hasH264 ? "YES" : "NO") << std::endl;
        std::cout << "  MJPEG: " << (info.hasMJPEG ? "YES" : "NO") << std::endl;
        std::cout << "  Streaming: " << (info.supportsStreaming ? "YES" : "NO") << std::endl;
        std::cout << "  Dual Access: " << (info.supportsDualAccess ? "YES" : "NO") << std::endl;
    }
    
    return deviceInfos;
}

void recommendMultiDeviceConfiguration(const std::vector<DeviceInfo>& devices) {
    std::cout << "\n=== Multi-Device Configuration Recommendations ===" << std::endl;
    
    if (devices.size() < 2) {
        std::cout << "Not enough devices for multi-device configuration" << std::endl;
        return;
    }
    
    // Find best H264 device
    std::string bestH264Device;
    std::string bestMJPEGDevice;
    
    for (const auto& device : devices) {
        if (device.hasH264 && device.supportsStreaming && bestH264Device.empty()) {
            bestH264Device = device.path;
        }
        if (device.hasMJPEG && device.supportsStreaming && bestMJPEGDevice.empty()) {
            bestMJPEGDevice = device.path;
        }
    }
    
    if (!bestH264Device.empty() && !bestMJPEGDevice.empty() && bestH264Device != bestMJPEGDevice) {
        std::cout << "✓ OPTIMAL CONFIGURATION FOUND:" << std::endl;
        std::cout << "  Primary stream (H264): " << bestH264Device << std::endl;
        std::cout << "  Snapshot source (MJPEG): " << bestMJPEGDevice << std::endl;
        std::cout << "\nRecommended v4l2rtspserver command:" << std::endl;
        std::cout << "  ./v4l2rtspserver -j -P 9999 -W 1920 -H 1080 -F 30 " << bestH264Device << std::endl;
        std::cout << "\nSnapshotManager will automatically use " << bestMJPEGDevice << " for real JPEG snapshots" << std::endl;
    } else if (!bestH264Device.empty()) {
        std::cout << "⚠ SINGLE DEVICE CONFIGURATION:" << std::endl;
        std::cout << "  Only H264 device available: " << bestH264Device << std::endl;
        std::cout << "  Snapshots will be informational SVG format" << std::endl;
        std::cout << "\nCommand:" << std::endl;
        std::cout << "  ./v4l2rtspserver -j -P 9999 -W 1920 -H 1080 -F 30 " << bestH264Device << std::endl;
    } else {
        std::cout << "✗ NO SUITABLE CONFIGURATION FOUND" << std::endl;
        std::cout << "  No devices support required formats" << std::endl;
    }
    
    // Additional analysis
    std::cout << "\nDevice Compatibility Matrix:" << std::endl;
    for (size_t i = 0; i < devices.size(); i++) {
        for (size_t j = i + 1; j < devices.size(); j++) {
            const auto& dev1 = devices[i];
            const auto& dev2 = devices[j];
            
            bool compatible = (dev1.hasH264 && dev2.hasMJPEG) || (dev1.hasMJPEG && dev2.hasH264);
            std::cout << "  " << dev1.path << " + " << dev2.path << ": " 
                      << (compatible ? "COMPATIBLE" : "not compatible") << std::endl;
        }
    }
}

void runComprehensiveMultiDeviceTest(const std::string& baseDevice) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "COMPREHENSIVE MULTI-DEVICE TEST" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // Find all related devices
    std::vector<std::string> relatedDevices;
    relatedDevices.push_back(baseDevice);
    
    std::string baseNum = baseDevice.substr(baseDevice.find_last_not_of("0123456789") + 1);
    int baseDeviceNum = std::stoi(baseNum);
    
    for (int offset = 1; offset <= 3; offset++) {
        std::string candidatePath = "/dev/video" + std::to_string(baseDeviceNum + offset);
        if (access(candidatePath.c_str(), F_OK) == 0) {
            relatedDevices.push_back(candidatePath);
        }
    }
    
    std::cout << "Found " << relatedDevices.size() << " related devices" << std::endl;
    
    // Run all tests
    std::vector<DeviceInfo> deviceInfos = analyzeDeviceGroup(relatedDevices);
    testConcurrentAccess(relatedDevices);
    
    if (relatedDevices.size() >= 2) {
        testMultiDeviceFormats(relatedDevices);
    }
    
    recommendMultiDeviceConfiguration(deviceInfos);
}

int main(int argc, char* argv[]) {
    std::cout << "V4L2 Device Inspector with Multi-Device Testing" << std::endl;
    std::cout << "=============================================" << std::endl;
    
    if (argc > 1) {
        std::string devicePath = argv[1];
        
        // Check for special flags
        if (argc > 2) {
            std::string flag = argv[2];
            if (flag == "--multi" || flag == "-m") {
                // Run comprehensive multi-device test
                runComprehensiveMultiDeviceTest(devicePath);
                return 0;
            }
        }
        
        // Standard single device inspection
        inspectDevice(devicePath);
        testDualAccess(devicePath);
        findRelatedDevices(devicePath);
        
        // Automatically run multi-device test if related devices found
        std::string baseNum = devicePath.substr(devicePath.find_last_not_of("0123456789") + 1);
        int baseDeviceNum = std::stoi(baseNum);
        
        bool hasRelatedDevices = false;
        for (int offset = 1; offset <= 3; offset++) {
            std::string candidatePath = "/dev/video" + std::to_string(baseDeviceNum + offset);
            if (access(candidatePath.c_str(), F_OK) == 0) {
                hasRelatedDevices = true;
                break;
            }
        }
        
        if (hasRelatedDevices) {
            std::cout << "\n" << std::string(50, '=') << std::endl;
            std::cout << "RELATED DEVICES DETECTED - RUNNING MULTI-DEVICE ANALYSIS" << std::endl;
            std::cout << std::string(50, '=') << std::endl;
            runComprehensiveMultiDeviceTest(devicePath);
        }
        
    } else {
        std::cout << "\nScanning for available devices..." << std::endl;
        
        std::vector<std::string> foundDevices;
        
        // Check for common video devices
        for (int i = 0; i < 8; i++) {
            std::string devicePath = "/dev/video" + std::to_string(i);
            
            if (access(devicePath.c_str(), F_OK) == 0) {
                foundDevices.push_back(devicePath);
                inspectDevice(devicePath);
            }
        }
        
        // If multiple devices found, run group analysis
        if (foundDevices.size() > 1) {
            std::cout << "\n" << std::string(60, '=') << std::endl;
            std::cout << "MULTIPLE DEVICES DETECTED - RUNNING GROUP ANALYSIS" << std::endl;
            std::cout << std::string(60, '=') << std::endl;
            
            std::vector<DeviceInfo> deviceInfos = analyzeDeviceGroup(foundDevices);
            testConcurrentAccess(foundDevices);
            
            if (foundDevices.size() >= 2) {
                testMultiDeviceFormats(foundDevices);
            }
            
            recommendMultiDeviceConfiguration(deviceInfos);
        }
        
        std::cout << "\n" << std::string(40, '=') << std::endl;
        std::cout << "USAGE INFORMATION" << std::endl;
        std::cout << std::string(40, '=') << std::endl;
        std::cout << "Basic usage:" << std::endl;
        std::cout << "  " << argv[0] << "                    - Scan all devices" << std::endl;
        std::cout << "  " << argv[0] << " /dev/video0         - Inspect specific device" << std::endl;
        std::cout << "  " << argv[0] << " /dev/video0 --multi - Comprehensive multi-device test" << std::endl;
        std::cout << "\nFlags:" << std::endl;
        std::cout << "  --multi, -m    - Run comprehensive multi-device testing" << std::endl;
    }
    
    return 0;
} 