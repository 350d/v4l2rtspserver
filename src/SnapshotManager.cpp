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
      m_width(0), m_height(0), m_snapshotWidth(640), m_snapshotHeight(480), 
      m_lastSnapshotTime(0), m_saveInterval(5), m_lastSaveTime(0) {
}

SnapshotManager::~SnapshotManager() {
    m_mjpegDevice.reset();
}

void SnapshotManager::setFrameDimensions(int width, int height) {
    m_width = width;
    m_height = height;
}

void SnapshotManager::setSnapshotResolution(int width, int height) {
    m_snapshotWidth = width > 0 ? width : 640;
    m_snapshotHeight = height > 0 ? height : 480;
    LOG(INFO) << "Snapshot resolution set to: " << m_snapshotWidth << "x" << m_snapshotHeight;
}

void SnapshotManager::setSaveInterval(int intervalSeconds) {
    // Validate range: 1-60 seconds
    if (intervalSeconds < 1) {
        intervalSeconds = 1;
        LOG(WARN) << "Save interval too low, set to minimum: 1 second";
    } else if (intervalSeconds > 60) {
        intervalSeconds = 60;
        LOG(WARN) << "Save interval too high, set to maximum: 60 seconds";
    }
    
    m_saveInterval = intervalSeconds;
    LOG(INFO) << "Snapshot save interval set to: " << m_saveInterval << " seconds";
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
    
    // Store primary device path for later use
    m_primaryDevicePath = primaryDevice;
    
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
        LOG(INFO) << "No related MJPEG device found for " << primaryDevice;
        return false;
    }
    
    LOG(INFO) << "Attempting to initialize MJPEG snapshot device: " << mjpegDevice;
    
    try {
        // Create MJPEG capture device with lower resolution for snapshots
        std::list<unsigned int> formatList;
        formatList.push_back(V4L2_PIX_FMT_MJPEG);
        
        // Use configurable resolution for snapshots (default 640x480)
        V4L2DeviceParameters params(mjpegDevice.c_str(), formatList, m_snapshotWidth, m_snapshotHeight, 1, IOTYPE_MMAP);
        
        auto device = std::unique_ptr<V4l2Capture>(V4l2Capture::create(params));
        if (device && device->isReady()) {
            m_mjpegDevice = std::move(device);
            m_mjpegDevicePath = mjpegDevice;
            LOG(NOTICE) << "Successfully initialized MJPEG snapshot device: " << mjpegDevice;
            return true;
        } else {
            LOG(WARN) << "Failed to create or ready MJPEG device: " << mjpegDevice;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to initialize MJPEG device " << mjpegDevice << ": " << e.what();
    }
    
    return false;
}

bool SnapshotManager::findRelatedMJPEGDevice(const std::string& baseDevice, std::string& mjpegDevice) {
    // Extract base device number (e.g., "/dev/video0" -> 0)
    size_t pos = baseDevice.find_last_not_of("0123456789");
    if (pos == std::string::npos) {
        LOG(WARN) << "Cannot extract device number from: " << baseDevice;
        return false;
    }
    
    std::string baseNum = baseDevice.substr(pos + 1);
    int baseDeviceNum = std::stoi(baseNum);
    
    LOG(INFO) << "Searching for MJPEG devices related to " << baseDevice << " (base number: " << baseDeviceNum << ")";
    
    // Check consecutive device numbers (many Pi cameras create video0, video1, etc.)
    for (int offset = 1; offset <= 3; offset++) {
        std::string candidatePath = "/dev/video" + std::to_string(baseDeviceNum + offset);
        
        LOG(DEBUG) << "Checking candidate device: " << candidatePath;
        
        if (access(candidatePath.c_str(), F_OK) == 0) {
            LOG(INFO) << "Device exists: " << candidatePath;
            bool supportsH264, supportsMJPEG;
            if (testDeviceFormats(candidatePath, supportsH264, supportsMJPEG)) {
                LOG(INFO) << "Device " << candidatePath << " - H264: " << (supportsH264 ? "YES" : "NO") 
                         << ", MJPEG: " << (supportsMJPEG ? "YES" : "NO");
                if (supportsMJPEG) {
                    LOG(NOTICE) << "Found related MJPEG device: " << candidatePath;
                    mjpegDevice = candidatePath;
                    return true;
                }
            } else {
                LOG(WARN) << "Failed to test formats for device: " << candidatePath;
            }
        } else {
            LOG(DEBUG) << "Device does not exist: " << candidatePath;
        }
    }
    
    LOG(INFO) << "No related MJPEG device found for " << baseDevice;
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
    
    // Release lock before auto-save to avoid blocking
    lock.~lock_guard();
    
    // Auto-save if file path is specified
    autoSaveSnapshot();
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

void SnapshotManager::processH264KeyframeWithSPS(const unsigned char* h264Data, size_t dataSize, 
                                                const std::string& sps, const std::string& pps, 
                                                int width, int height) {
    if (!m_enabled || !h264Data || dataSize == 0) {
        return;
    }
    
    // If we have a separate MJPEG device, try to capture from it first
    if (m_mode == SnapshotMode::MJPEG_DEVICE && m_mjpegDevice) {
        if (captureMJPEGSnapshot()) {
            return; // Successfully captured real MJPEG snapshot
        }
    }
    
    // For MJPEG_STREAM mode, try to find and use a separate MJPEG device
    if (m_mode == SnapshotMode::MJPEG_STREAM) {
        // Try to initialize MJPEG device on first H264 keyframe
        if (tryInitializeMJPEGDevice(m_primaryDevicePath)) {
            m_mode = SnapshotMode::MJPEG_DEVICE;
            LOG(NOTICE) << "Upgraded to MJPEG device mode for real snapshots";
            if (captureMJPEGSnapshot()) {
                return; // Successfully captured real MJPEG snapshot
            }
        }
        
        // If no MJPEG device available, create informational snapshot instead of MP4
        createH264InfoSnapshot(dataSize, width, height);
        return;
    }
    
    // Only create MP4 snapshots if explicitly in H264_MP4 mode
    if (m_mode == SnapshotMode::H264_MP4 && !sps.empty() && !pps.empty()) {
        createH264MP4Snapshot(h264Data, dataSize, sps, pps, width, height);
    } else {
        // Fallback to SVG info snapshot
        createH264InfoSnapshot(dataSize, width, height);
    }
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
    
    LOG(DEBUG) << "H264 fallback snapshot (SVG) created: " << m_currentSnapshot.size() << " bytes for " 
               << h264Size << " bytes of H264 data (" << width << "x" << height << ")";
    
    // Auto-save if file path is specified
    autoSaveSnapshot();
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
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    
    // Check actual content type if we have data
    if (!m_currentSnapshot.empty()) {
        // Check for JPEG magic bytes (FF D8 FF)
        if (m_currentSnapshot.size() >= 3 && 
            m_currentSnapshot[0] == 0xFF && 
            m_currentSnapshot[1] == 0xD8 && 
            m_currentSnapshot[2] == 0xFF) {
            return "image/jpeg";
        }
        
        // Check for SVG content (starts with "<?xml" or "<svg")
        if (m_currentSnapshot.size() >= 5) {
            std::string start(m_currentSnapshot.begin(), m_currentSnapshot.begin() + 5);
            if (start == "<?xml" || start == "<svg ") {
                return "image/svg+xml";
            }
        }
        
        // Check for MP4 magic bytes (ftyp)
        if (m_currentSnapshot.size() >= 8) {
            std::string ftyp(m_currentSnapshot.begin() + 4, m_currentSnapshot.begin() + 8);
            if (ftyp == "ftyp") {
                return "video/mp4";
            }
        }
    }
    
    // Fallback to mode-based detection
    switch (m_mode) {
        case SnapshotMode::MJPEG_STREAM:
        case SnapshotMode::MJPEG_DEVICE:
            return "image/jpeg";
        case SnapshotMode::H264_MP4:
            return "video/mp4";
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
        case SnapshotMode::H264_MP4:
            return "H264 MP4 (mini video snapshots with keyframes)";
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

void SnapshotManager::createH264MP4Snapshot(const unsigned char* h264Data, size_t h264Size, 
                                           const std::string& sps, const std::string& pps, 
                                           int width, int height) {
    // Create minimal but valid MP4 container with single H264 frame
    int actualWidth = width > 0 ? width : (m_width > 0 ? m_width : 640);
    int actualHeight = height > 0 ? height : (m_height > 0 ? m_height : 480);
    
    std::vector<unsigned char> mp4Data;
    
    // Helper lambda to write 32-bit big-endian value
    auto writeBE32 = [&mp4Data](uint32_t value) {
        mp4Data.push_back((value >> 24) & 0xFF);
        mp4Data.push_back((value >> 16) & 0xFF);
        mp4Data.push_back((value >> 8) & 0xFF);
        mp4Data.push_back(value & 0xFF);
    };
    
    // Helper lambda to write 16-bit big-endian value
    auto writeBE16 = [&mp4Data](uint16_t value) {
        mp4Data.push_back((value >> 8) & 0xFF);
        mp4Data.push_back(value & 0xFF);
    };
    
    // Helper lambda to write string
    auto writeString = [&mp4Data](const std::string& str) {
        mp4Data.insert(mp4Data.end(), str.begin(), str.end());
    };
    
    // 1. FTYP box (File Type)
    size_t ftypStart = mp4Data.size();
    writeBE32(0); // size placeholder
    writeString("ftyp");
    writeString("mp42");           // major brand
    writeBE32(0);                  // minor version  
    writeString("mp42isom");       // compatible brands
    
    // Update ftyp size
    uint32_t ftypSize = mp4Data.size() - ftypStart;
    mp4Data[ftypStart] = (ftypSize >> 24) & 0xFF;
    mp4Data[ftypStart + 1] = (ftypSize >> 16) & 0xFF;
    mp4Data[ftypStart + 2] = (ftypSize >> 8) & 0xFF;
    mp4Data[ftypStart + 3] = ftypSize & 0xFF;
    
    // 2. MOOV box (Movie metadata)
    size_t moovStart = mp4Data.size();
    writeBE32(0); // size placeholder
    writeString("moov");
    
    // 2.1 MVHD box (Movie header)
    size_t mvhdStart = mp4Data.size();
    writeBE32(0); // size placeholder
    writeString("mvhd");
    writeBE32(0);                  // version & flags
    writeBE32(0);                  // creation time
    writeBE32(0);                  // modification time  
    writeBE32(1000);               // timescale (1000 units per second)
    writeBE32(100);                // duration (0.1 second)
    writeBE32(0x00010000);         // preferred rate (1.0)
    writeBE16(0x0100);             // preferred volume (1.0)
    // Reserved
    for (int i = 0; i < 10; i++) mp4Data.push_back(0);
    // Matrix structure (identity matrix)
    writeBE32(0x00010000); writeBE32(0); writeBE32(0);
    writeBE32(0); writeBE32(0x00010000); writeBE32(0);
    writeBE32(0); writeBE32(0); writeBE32(0x40000000);
    // Preview time/duration, poster time, selection time/duration, current time
    for (int i = 0; i < 6; i++) writeBE32(0);
    writeBE32(2);                  // next track ID
    
    // Update mvhd size
    uint32_t mvhdSize = mp4Data.size() - mvhdStart;
    mp4Data[mvhdStart] = (mvhdSize >> 24) & 0xFF;
    mp4Data[mvhdStart + 1] = (mvhdSize >> 16) & 0xFF;
    mp4Data[mvhdStart + 2] = (mvhdSize >> 8) & 0xFF;
    mp4Data[mvhdStart + 3] = mvhdSize & 0xFF;
    
    // 2.2 TRAK box (Track container)
    size_t trakStart = mp4Data.size();
    writeBE32(0); // size placeholder
    writeString("trak");
    
    // 2.2.1 TKHD box (Track header)
    size_t tkhdStart = mp4Data.size();
    writeBE32(0); // size placeholder
    writeString("tkhd");
    writeBE32(0x000000007);        // version & flags (track enabled)
    writeBE32(0);                  // creation time
    writeBE32(0);                  // modification time
    writeBE32(1);                  // track ID
    writeBE32(0);                  // reserved
    writeBE32(100);                // duration
    writeBE32(0); writeBE32(0);    // reserved
    writeBE16(0);                  // layer
    writeBE16(0);                  // alternate group
    writeBE16(0);                  // volume
    writeBE16(0);                  // reserved
    // Matrix structure (identity matrix)
    writeBE32(0x00010000); writeBE32(0); writeBE32(0);
    writeBE32(0); writeBE32(0x00010000); writeBE32(0);
    writeBE32(0); writeBE32(0); writeBE32(0x40000000);
    writeBE32(actualWidth << 16);  // track width
    writeBE32(actualHeight << 16); // track height
    
    // Update tkhd size
    uint32_t tkhdSize = mp4Data.size() - tkhdStart;
    mp4Data[tkhdStart] = (tkhdSize >> 24) & 0xFF;
    mp4Data[tkhdStart + 1] = (tkhdSize >> 16) & 0xFF;
    mp4Data[tkhdStart + 2] = (tkhdSize >> 8) & 0xFF;
    mp4Data[tkhdStart + 3] = tkhdSize & 0xFF;
    
    // Update trak size
    uint32_t trakSize = mp4Data.size() - trakStart;
    mp4Data[trakStart] = (trakSize >> 24) & 0xFF;
    mp4Data[trakStart + 1] = (trakSize >> 16) & 0xFF;
    mp4Data[trakStart + 2] = (trakSize >> 8) & 0xFF;
    mp4Data[trakStart + 3] = trakSize & 0xFF;
    
    // Update moov size
    uint32_t moovSize = mp4Data.size() - moovStart;
    mp4Data[moovStart] = (moovSize >> 24) & 0xFF;
    mp4Data[moovStart + 1] = (moovSize >> 16) & 0xFF;
    mp4Data[moovStart + 2] = (moovSize >> 8) & 0xFF;
    mp4Data[moovStart + 3] = moovSize & 0xFF;
    
    // 3. MDAT box (Media data) - simplified approach
    // For a single frame, we'll include SPS, PPS, and IDR frame with Annex B start codes
    size_t dataSize = 4 + sps.size() + 4 + pps.size() + 4 + h264Size;
    writeBE32(8 + dataSize);       // mdat box size
    writeString("mdat");
    
    // Add NAL units with Annex B start codes
    writeBE32(0x00000001);         // start code
    mp4Data.insert(mp4Data.end(), sps.begin(), sps.end());
    
    writeBE32(0x00000001);         // start code
    mp4Data.insert(mp4Data.end(), pps.begin(), pps.end());
    
    writeBE32(0x00000001);         // start code
    mp4Data.insert(mp4Data.end(), h264Data, h264Data + h264Size);
    
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_currentSnapshot = mp4Data;
    m_lastSnapshotTime = std::time(nullptr);
    
    LOG(DEBUG) << "H264 MP4 snapshot created: " << mp4Data.size() << " bytes (" 
               << actualWidth << "x" << actualHeight << "), SPS:" << sps.size() 
               << " PPS:" << pps.size() << " IDR:" << h264Size;
    
    // Auto-save if file path is specified
    autoSaveSnapshot();
}

void SnapshotManager::autoSaveSnapshot() {
    if (m_filePath.empty()) {
        return;
    }
    
    // Check save interval
    std::time_t now = std::time(nullptr);
    if (m_lastSaveTime > 0 && (now - m_lastSaveTime) < m_saveInterval) {
        // Too soon to save again
        return;
    }
    
    std::vector<unsigned char> dataToSave;
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        dataToSave = m_currentSnapshot;
    }
    
    if (dataToSave.empty()) {
        return;
    }
    
    try {
        std::ofstream file(m_filePath, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(dataToSave.data()), dataToSave.size());
            file.close();
            if (file.good()) {
                m_lastSaveTime = now; // Update save time only on successful save
                LOG(DEBUG) << "Auto-saved snapshot: " << m_filePath << " (" << dataToSave.size() << " bytes)";
            } else {
                LOG(ERROR) << "Error writing snapshot to file: " << m_filePath;
            }
        } else {
            LOG(ERROR) << "Failed to open file for writing: " << m_filePath;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception while auto-saving snapshot: " << e.what();
    } catch (...) {
        LOG(ERROR) << "Unknown exception while auto-saving snapshot";
    }
}

bool SnapshotManager::saveSnapshotToFile() {
    if (m_filePath.empty()) {
        LOG(ERROR) << "No file path specified for snapshot saving";
        return false;
    }
    return saveSnapshotToFile(m_filePath);
}

bool SnapshotManager::saveSnapshotToFile(const std::string& filePath) {
    if (!m_enabled) {
        LOG(WARN) << "Snapshots are disabled";
        return false;
    }
    
    std::vector<unsigned char> snapshotData;
    if (!getSnapshot(snapshotData) || snapshotData.empty()) {
        LOG(WARN) << "No snapshot data available for saving";
        return false;
    }
    
    try {
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            LOG(ERROR) << "Failed to open file for writing: " << filePath;
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(snapshotData.data()), snapshotData.size());
        file.close();
        
        if (file.good()) {
            LOG(NOTICE) << "Snapshot saved to file: " << filePath << " (" << snapshotData.size() << " bytes)";
            return true;
        } else {
            LOG(ERROR) << "Error writing snapshot to file: " << filePath;
            return false;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception while saving snapshot: " << e.what();
        return false;
    }
} 