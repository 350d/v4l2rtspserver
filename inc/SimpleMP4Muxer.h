/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** SimpleMP4Muxer.h
** 
** Simple MP4 muxer for efficient H264 video recording
**
** -------------------------------------------------------------------------*/

#pragma once

#include <string>
#include <vector>
#include <cstdint>

class SimpleMP4Muxer {
public:
    SimpleMP4Muxer();
    ~SimpleMP4Muxer();
    
    // Initialize MP4 file with SPS/PPS and dimensions
    bool initialize(int fd, const std::string& sps, const std::string& pps, int width, int height);
    
    // Add H264 frame (will handle keyframes and regular frames)
    bool addFrame(const unsigned char* h264Data, size_t dataSize, bool isKeyFrame);
    
    // Finalize MP4 file (update metadata)
    bool finalize();
    
    // Check if muxer is initialized
    bool isInitialized() const { return m_initialized; }
    
private:
    int m_fd;
    bool m_initialized;
    std::string m_sps;
    std::string m_pps;
    int m_width;
    int m_height;
    
    // Track file positions for metadata updates
    size_t m_mdatStartPos;
    size_t m_currentPos;
    
    // Frame counting
    uint32_t m_frameCount;
    uint32_t m_keyFrameCount;
    
    // Helper methods
    void write32(uint32_t value);
    void write16(uint16_t value);
    void write8(uint8_t value);
    void writeBytes(const void* data, size_t size);
    void writeString(const std::string& str);
    
    bool writeMP4Header();
    bool writeFtypBox();
    bool writeMoovBox();
    bool startMdatBox();
    bool updateMdatSize();
}; 