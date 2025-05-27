/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** SnapshotManager.h
** 
** Real Image Snapshot Manager for v4l2rtspserver
**
** -------------------------------------------------------------------------*/

#pragma once

#include <vector>
#include <mutex>
#include <string>
#include <memory>
#include <ctime>

// Forward declaration
class V4l2Capture;
struct V4L2DeviceParameters;

enum class SnapshotMode {
    DISABLED,
    MJPEG_STREAM,     // Real JPEG snapshots from MJPEG stream
    MJPEG_DEVICE,     // Real JPEG snapshots from separate MJPEG device
    H264_FALLBACK,    // Informational SVG snapshots for H264 streams
    H264_MP4          // Mini MP4 snapshots with H264 keyframes
};

class SnapshotManager {
public:
    static SnapshotManager& getInstance() {
        static SnapshotManager instance;
        return instance;
    }

    // Configuration
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    void setFrameDimensions(int width, int height);
    void setSnapshotResolution(int width, int height);
    void setFilePath(const std::string& filePath) { m_filePath = filePath; }
    void setSaveInterval(int intervalSeconds);  // Validates range 1-60 seconds
    
    // Initialization
    bool initialize(int width, int height);
    bool initializeWithDevice(const std::string& primaryDevice);
    
    // Frame processing (called by existing video sources)
    void processMJPEGFrame(const unsigned char* jpegData, size_t dataSize);
    void processH264Keyframe(const unsigned char* h264Data, size_t dataSize, int width, int height);
    void processH264KeyframeWithSPS(const unsigned char* h264Data, size_t dataSize, 
                                   const std::string& sps, const std::string& pps, 
                                   int width, int height);
    void processRawFrame(const unsigned char* yuvData, size_t dataSize, int width, int height);
    
    // Snapshot retrieval
    bool getSnapshot(std::vector<unsigned char>& jpegData);
    std::string getSnapshotMimeType() const;
    
    // File operations
    bool saveSnapshotToFile();
    bool saveSnapshotToFile(const std::string& filePath);
    
    // Status
    SnapshotMode getMode() const { return m_mode; }
    std::string getModeDescription() const;
    bool hasRecentSnapshot() const;
    
private:
    SnapshotManager();
    ~SnapshotManager();
    SnapshotManager(const SnapshotManager&) = delete;
    SnapshotManager& operator=(const SnapshotManager&) = delete;
    
    // Device management
    bool tryInitializeMJPEGDevice(const std::string& primaryDevice);
    bool findRelatedMJPEGDevice(const std::string& baseDevice, std::string& mjpegDevice);
    std::vector<std::string> findVideoDevices();
    bool testDeviceFormats(const std::string& devicePath, bool& supportsH264, bool& supportsMJPEG);
    
    // Snapshot creation
    void createH264InfoSnapshot(size_t h264Size, int width, int height);
    bool captureMJPEGSnapshot();
    void createH264MP4Snapshot(const unsigned char* h264Data, size_t h264Size, 
                              const std::string& sps, const std::string& pps, 
                              int width, int height);
    void autoSaveSnapshot();
    
    // Members
    bool m_enabled;
    SnapshotMode m_mode;
    int m_width;
    int m_height;
    int m_snapshotWidth;
    int m_snapshotHeight;
    
    // Thread safety
    mutable std::mutex m_snapshotMutex;
    std::vector<unsigned char> m_currentSnapshot;
    std::time_t m_lastSnapshotTime;
    
    // MJPEG device for separate capture
    std::unique_ptr<V4l2Capture> m_mjpegDevice;
    std::string m_mjpegDevicePath;
    
    // File operations
    std::string m_filePath;
    int m_saveInterval;
    std::time_t m_lastSaveTime;
}; 