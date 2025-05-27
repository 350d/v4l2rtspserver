/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** SnapshotManager.cpp
** 
** Real Image Snapshot Manager implementation
**
** -------------------------------------------------------------------------*/

#include "SnapshotManager.h"
#include "logger.h"
#include "../libv4l2cpp/inc/V4l2Capture.h"
#include <cstring>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#ifdef __linux__
#include <linux/videodev2.h>
#endif

SnapshotManager::SnapshotManager() 
    : m_enabled(false), m_mode(SnapshotMode::DISABLED), 
      m_width(0), m_height(0), m_lastSnapshotTime(0) {
}

SnapshotManager::~SnapshotManager() {
    m_mjpegDevice.reset();
}

void SnapshotManager::setFrameDimensions(int width, int height) {
    m_width = width;
    m_height = height;
}

bool SnapshotManager::initialize(int width, int height) {
    m_width = width;
    m_height = height;
    
    if (!m_enabled) {
        m_mode = SnapshotMode::DISABLED;
        return true;
    }
    
    // Default to H264 fallback mode
    m_mode = SnapshotMode::H264_FALLBACK;
    LOG(NOTICE) << "SnapshotManager initialized in H264 fallback mode";
    return true;
}

bool SnapshotManager::initializeWithDevice(const std::string& primaryDevice) {
    if (!m_enabled) {
        m_mode = SnapshotMode::DISABLED;
        return true;
    }
    
    LOG(INFO) << "Initializing snapshot manager for device: " << primaryDevice;
    
    // First check what formats the primary device supports
    bool supportsH264 = false;
    bool supportsMJPEG = false;
    testDeviceFormats(primaryDevice, supportsH264, supportsMJPEG);
    
    if (supportsMJPEG) {
        // Try to find a separate MJPEG device for snapshots
        if (tryInitializeMJPEGDevice(primaryDevice)) {
            m_mode = SnapshotMode::MJPEG_DEVICE;
            LOG(NOTICE) << "Snapshot mode: Separate MJPEG device (" << m_mjpegDevicePath << ")";
            return true;
        }
        
        // If no separate device, we can use stream-based snapshots when MJPEG stream is active
        m_mode = SnapshotMode::MJPEG_STREAM;
        LOG(NOTICE) << "Snapshot mode: MJPEG stream-based (real snapshots only when camera is in MJPEG mode)";
        return true;
    }
    
    // Fallback to info snapshots for H264-only devices
    m_mode = SnapshotMode::H264_FALLBACK;
    LOG(NOTICE) << "Snapshot mode: H264 fallback (info snapshots only)";
    return true;
}

bool SnapshotManager::tryInitializeMJPEGDevice(const std::string& primaryDevice) {
    std::string mjpegDevice;
    
    // Try to find a related MJPEG device
    if (!findRelatedMJPEGDevice(primaryDevice, mjpegDevice)) {
        return false;
    }
    
    LOG(INFO) << "Attempting to initialize MJPEG snapshot device: " << mjpegDevice;
    
    try {
        // Create MJPEG capture device with lower resolution for snapshots
        std::list<unsigned int> formatList;
        formatList.push_back(V4L2_PIX_FMT_MJPEG);
        
        // Use 640x480 for snapshots to be efficient
        V4L2DeviceParameters params(mjpegDevice.c_str(), formatList, 640, 480, 1, IOTYPE_MMAP);
        
        auto device = std::unique_ptr<V4l2Capture>(V4l2Capture::create(params));
        if (device && device->isReady()) {
            m_mjpegDevice = std::move(device);
            m_mjpegDevicePath = mjpegDevice;
            LOG(INFO) << "Successfully initialized MJPEG snapshot device: " << mjpegDevice;
            return true;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to initialize MJPEG device: " << e.what();
    }
    
    return false;
}

bool SnapshotManager::findRelatedMJPEGDevice(const std::string& baseDevice, std::string& mjpegDevice) {
    // Extract base device number (e.g., "/dev/video0" -> 0)
    size_t pos = baseDevice.find_last_not_of("0123456789");
    if (pos == std::string::npos) return false;
    
    std::string baseNum = baseDevice.substr(pos + 1);
    int baseDeviceNum = std::stoi(baseNum);
    
    // Check consecutive device numbers (many Pi cameras create video0, video1, etc.)
    for (int offset = 1; offset <= 3; offset++) {
        std::string candidatePath = "/dev/video" + std::to_string(baseDeviceNum + offset);
        
        if (access(candidatePath.c_str(), F_OK) == 0) {
            bool supportsH264, supportsMJPEG;
            if (testDeviceFormats(candidatePath, supportsH264, supportsMJPEG) && supportsMJPEG) {
                LOG(INFO) << "Found related MJPEG device: " << candidatePath;
                mjpegDevice = candidatePath;
                return true;
            }
        }
    }
    
    return false;
}

bool SnapshotManager::testDeviceFormats(const std::string& devicePath, bool& supportsH264, bool& supportsMJPEG) {
    supportsH264 = false;
    supportsMJPEG = false;
    
#ifdef __linux__
    int fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    
    for (fmtdesc.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0; fmtdesc.index++) {
        if (fmtdesc.pixelformat == V4L2_PIX_FMT_H264) {
            supportsH264 = true;
        }
        if (fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG) {
            supportsMJPEG = true;
        }
    }
    
    close(fd);
    return true;
#else
    // On non-Linux platforms, assume basic support
    return false;
#endif
}

void SnapshotManager::processMJPEGFrame(const unsigned char* jpegData, size_t dataSize) {
    if (!m_enabled || !jpegData || dataSize == 0) {
        return;
    }
    
    // This is called from MJPEGVideoSource - we have real JPEG data!
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_currentSnapshot.assign(jpegData, jpegData + dataSize);
    m_lastSnapshotTime = std::time(nullptr);
    
    // Update mode if we weren't sure before
    if (m_mode == SnapshotMode::H264_FALLBACK) {
        m_mode = SnapshotMode::MJPEG_STREAM;
    }
    
    LOG(DEBUG) << "Real MJPEG snapshot captured: " << dataSize << " bytes";
}

void SnapshotManager::processH264Keyframe(const unsigned char* h264Data, size_t dataSize, int width, int height) {
    if (!m_enabled || !h264Data || dataSize == 0) {
        return;
    }
    
    // If we have a separate MJPEG device, try to capture from it
    if (m_mode == SnapshotMode::MJPEG_DEVICE && m_mjpegDevice) {
        if (captureMJPEGSnapshot()) {
            return; // Successfully captured real MJPEG snapshot
        }
    }
    
    // Fallback to info snapshot
    createH264InfoSnapshot(dataSize, width, height);
}

void SnapshotManager::processRawFrame(const unsigned char* yuvData, size_t dataSize, int width, int height) {
    if (!m_enabled || !yuvData || dataSize == 0) {
        return;
    }
    
    // Try to capture from MJPEG device if available
    if (m_mode == SnapshotMode::MJPEG_DEVICE && m_mjpegDevice) {
        captureMJPEGSnapshot();
    }
    
    // For raw frames, we could implement YUV->JPEG conversion here if needed
    LOG(DEBUG) << "Raw frame processed for snapshots";
}

bool SnapshotManager::captureMJPEGSnapshot() {
    if (!m_mjpegDevice) {
        return false;
    }
    
    try {
        // Start device if not already started
        if (!m_mjpegDevice->start()) {
            LOG(ERROR) << "Failed to start MJPEG snapshot device";
            return false;
        }
        
        // Capture a frame (this is blocking, but should be fast for snapshots)
        std::vector<char> buffer(m_mjpegDevice->getBufferSize());
        size_t bytesRead = m_mjpegDevice->read(buffer.data(), buffer.size());
        
        if (bytesRead > 0) {
            std::lock_guard<std::mutex> lock(m_snapshotMutex);
            m_currentSnapshot.assign(
                reinterpret_cast<unsigned char*>(buffer.data()),
                reinterpret_cast<unsigned char*>(buffer.data()) + bytesRead
            );
            m_lastSnapshotTime = std::time(nullptr);
            
            LOG(DEBUG) << "Captured MJPEG snapshot from separate device: " << bytesRead << " bytes";
            return true;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Error capturing MJPEG snapshot: " << e.what();
    }
    
    return false;
}

void SnapshotManager::createH264InfoSnapshot(size_t h264Size, int width, int height) {
    // Create informational SVG for H264 streams
    int actualWidth = width > 0 ? width : m_width;
    int actualHeight = height > 0 ? height : m_height;
    
    std::time_t now = std::time(nullptr);
    char timeStr[100];
    std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", std::localtime(&now));
    
    std::string svgContent = 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg width=\"400\" height=\"300\" xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <rect width=\"100%\" height=\"100%\" fill=\"#f8f9fa\"/>\n"
        "  <rect x=\"10\" y=\"10\" width=\"380\" height=\"280\" fill=\"#ffffff\" stroke=\"#dee2e6\" stroke-width=\"2\" rx=\"8\"/>\n"
        "  <text x=\"200\" y=\"50\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"20\" font-weight=\"bold\" fill=\"#495057\">H264 Stream Active</text>\n"
        "  <text x=\"200\" y=\"100\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"14\" fill=\"#6c757d\">Resolution: " + 
        std::to_string(actualWidth) + "x" + std::to_string(actualHeight) + "</text>\n"
        "  <text x=\"200\" y=\"130\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"14\" fill=\"#6c757d\">Keyframe: " + 
        std::to_string(h264Size) + " bytes</text>\n"
        "  <text x=\"200\" y=\"160\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"14\" fill=\"#6c757d\">Time: " + 
        std::string(timeStr) + "</text>\n"
        "  <rect x=\"50\" y=\"180\" width=\"300\" height=\"60\" fill=\"#e9ecef\" stroke=\"#adb5bd\" stroke-width=\"1\" rx=\"4\"/>\n"
        "  <text x=\"200\" y=\"200\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"12\" fill=\"#495057\">For real image snapshots:</text>\n"
        "  <text x=\"200\" y=\"220\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"11\" fill=\"#6c757d\">Switch camera to MJPEG format</text>\n"
        "  <text x=\"200\" y=\"235\" text-anchor=\"middle\" font-family=\"monospace\" font-size=\"9\" fill=\"#868e96\">v4l2-ctl --set-fmt-video=pixelformat=MJPG</text>\n"
        "</svg>";
    
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_currentSnapshot.assign(svgContent.begin(), svgContent.end());
    m_lastSnapshotTime = std::time(nullptr);
    
    LOG(DEBUG) << "H264 info snapshot created";
}

bool SnapshotManager::getSnapshot(std::vector<unsigned char>& jpegData) {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    
    if (m_currentSnapshot.empty()) {
        return false;
    }
    
    jpegData = m_currentSnapshot;
    return true;
}

std::string SnapshotManager::getSnapshotMimeType() const {
    switch (m_mode) {
        case SnapshotMode::MJPEG_STREAM:
        case SnapshotMode::MJPEG_DEVICE:
            return "image/jpeg";
        case SnapshotMode::H264_FALLBACK:
            return "image/svg+xml";
        default:
            return "text/plain";
    }
}

std::string SnapshotManager::getModeDescription() const {
    switch (m_mode) {
        case SnapshotMode::DISABLED:
            return "Disabled";
        case SnapshotMode::MJPEG_STREAM:
            return "MJPEG Stream (real images when MJPEG active)";
        case SnapshotMode::MJPEG_DEVICE:
            return "Separate MJPEG Device (always real images)";
        case SnapshotMode::H264_FALLBACK:
            return "H264 Fallback (info snapshots only)";
        default:
            return "Unknown";
    }
}

bool SnapshotManager::hasRecentSnapshot() const {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    if (m_lastSnapshotTime == 0) {
        return false;
    }
    
    // Consider snapshot recent if it's less than 30 seconds old
    std::time_t now = std::time(nullptr);
    return (now - m_lastSnapshotTime) < 30;
}

std::vector<std::string> SnapshotManager::findVideoDevices() {
    std::vector<std::string> devices;
    
    for (int i = 0; i < 8; i++) {
        std::string devicePath = "/dev/video" + std::to_string(i);
        if (access(devicePath.c_str(), F_OK) == 0) {
            devices.push_back(devicePath);
        }
    }
    
    return devices;
} 