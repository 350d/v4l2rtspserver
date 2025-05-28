/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** SnapshotManagerSimple.h
** 
** Simplified Real Image Snapshot Manager for v4l2rtspserver
** Optimized for Pi Zero with modern camera stack
**
** -------------------------------------------------------------------------*/

#pragma once

#include <vector>
#include <mutex>
#include <string>
#include <ctime>

enum class SnapshotMode {
    DISABLED,
    MJPEG_STREAM,     // Real JPEG snapshots from MJPEG stream
    H264_FALLBACK,    // Informational SVG snapshots for H264 streams
    YUV_CONVERTED     // Real images converted from YUV data
};

class SnapshotManagerSimple {
public:
    static SnapshotManagerSimple& getInstance() {
        static SnapshotManagerSimple instance;
        return instance;
    }

    // Configuration
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    void setFrameDimensions(int width, int height);
    void setFilePath(const std::string& filePath) { m_filePath = filePath; }
    void setSaveInterval(int intervalSeconds);  // Validates range 1-60 seconds
    
    // Initialization
    bool initialize(int width, int height);
    
    // Frame processing (called by existing video sources)
    void processMJPEGFrame(const unsigned char* jpegData, size_t dataSize);
    void processH264Keyframe(const unsigned char* h264Data, size_t dataSize, int width, int height);
    void processRawFrame(const unsigned char* yuvData, size_t dataSize, int width, int height);
    
    // Snapshot retrieval
    bool getSnapshot(std::vector<unsigned char>& data);
    std::string getSnapshotMimeType() const;
    
    // File operations
    bool saveSnapshotToFile();
    bool saveSnapshotToFile(const std::string& filePath);
    
    // Status
    SnapshotMode getMode() const { return m_mode; }
    std::string getModeDescription() const;
    bool hasRecentSnapshot() const;
    
private:
    SnapshotManagerSimple();
    ~SnapshotManagerSimple() = default;
    SnapshotManagerSimple(const SnapshotManagerSimple&) = delete;
    SnapshotManagerSimple& operator=(const SnapshotManagerSimple&) = delete;
    
    // Snapshot creation
    void createH264InfoSnapshot(size_t h264Size, int width, int height);
    bool convertYUVToJPEG(const unsigned char* yuvData, size_t dataSize, int width, int height);
    void autoSaveSnapshot();
    
    // Members
    bool m_enabled = false;
    SnapshotMode m_mode = SnapshotMode::DISABLED;
    int m_width = 0;
    int m_height = 0;
    
    // Thread safety
    mutable std::mutex m_snapshotMutex;
    std::vector<unsigned char> m_currentSnapshot;
    std::time_t m_lastSnapshotTime = 0;
    
    // File operations
    std::string m_filePath;
    int m_saveInterval = 5;
    std::time_t m_lastSaveTime = 0;
}; 