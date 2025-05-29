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
#include <chrono>

// Forward declaration
struct V4L2DeviceParameters;

enum class SnapshotMode {
    DISABLED,
    MJPEG_STREAM,     // Real JPEG snapshots from MJPEG stream
    H264_FALLBACK,    // MP4 snapshots with cached H264 frames
    H264_MP4,         // Mini MP4 snapshots with H264 keyframes
    YUV_CONVERTED     // Real images converted from YUV data
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
    
    // Device format information
    void setDeviceFormat(unsigned int v4l2Format, int width, int height);
    void setPixelFormat(const std::string& pixelFormat);
    std::string getPixelFormat() const;
    unsigned int getV4L2Format() const;
    
    // Initialization
    bool initialize(int width, int height);
    
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
    
    // Pixel format conversion helpers
    std::string v4l2FormatToPixelFormat(unsigned int v4l2Format);
    std::string v4l2FormatToString(unsigned int v4l2Format);

    // Debug helper functions
    std::string getNALTypeName(uint8_t nalType);
    std::string getCurrentTimestamp();

    // Enhanced MP4 creation with debugging
    std::string createMP4Snapshot(const std::vector<uint8_t>& spsData, 
                                 const std::vector<uint8_t>& ppsData, 
                                 const std::vector<uint8_t>& h264Data,
                                 int width, int height);

private:
    SnapshotManager();
    ~SnapshotManager();
    SnapshotManager(const SnapshotManager&) = delete;
    SnapshotManager& operator=(const SnapshotManager&) = delete;
    
    // Snapshot creation
    void createH264Snapshot(const unsigned char* h264Data, size_t h264Size, 
                           int width, int height,
                           const std::string& sps = "", const std::string& pps = "");
    void autoSaveSnapshot();
    bool convertYUVToJPEG(const unsigned char* yuvData, size_t dataSize, int width, int height);
    
    // Dynamic NAL unit extraction (inspired by go2rtc)
    std::vector<uint8_t> findNALUnit(const uint8_t* data, size_t size, uint8_t nalType);
    
    // Members
    bool m_enabled;
    SnapshotMode m_mode;
    int m_width;
    int m_height;
    int m_snapshotWidth;
    int m_snapshotHeight;
    
    // Device format information
    unsigned int m_v4l2Format;
    std::string m_pixelFormat;
    bool m_formatInitialized;
    
    // Thread safety
    mutable std::mutex m_snapshotMutex;
    std::vector<unsigned char> m_currentSnapshot;
    std::time_t m_lastSnapshotTime;
    
    // Snapshot data storage
    std::vector<unsigned char> m_snapshotData;
    std::string m_snapshotMimeType;
    std::chrono::steady_clock::time_point m_lastSnapshotTimePoint;
    
    // File operations
    std::string m_filePath;
    int m_saveInterval;
    std::time_t m_lastSaveTime;
    
    // H264 frame cache for snapshots
    std::vector<unsigned char> m_lastH264Frame;
    std::string m_lastSPS;
    std::string m_lastPPS;
    int m_lastFrameWidth;
    int m_lastFrameHeight;
}; 