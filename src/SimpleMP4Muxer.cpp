/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** SimpleMP4Muxer.cpp
** 
** Simple MP4 muxer for efficient H264 video recording
**
** -------------------------------------------------------------------------*/

#include "../inc/SimpleMP4Muxer.h"
#include "../libv4l2cpp/inc/logger.h"
#include <unistd.h>
#include <cstring>

SimpleMP4Muxer::SimpleMP4Muxer() 
    : m_fd(-1), m_initialized(false), m_width(0), m_height(0),
      m_mdatStartPos(0), m_currentPos(0), m_frameCount(0), m_keyFrameCount(0) {
}

SimpleMP4Muxer::~SimpleMP4Muxer() {
    if (m_initialized) {
        finalize();
    }
}

bool SimpleMP4Muxer::initialize(int fd, const std::string& sps, const std::string& pps, int width, int height) {
    if (fd < 0 || sps.empty() || pps.empty() || width <= 0 || height <= 0) {
        LOG(ERROR) << "[MP4Muxer] Invalid initialization parameters";
        return false;
    }
    
    m_fd = fd;
    m_sps = sps;
    m_pps = pps;
    m_width = width;
    m_height = height;
    m_frameCount = 0;
    m_keyFrameCount = 0;
    
    // Write MP4 header structure
    if (!writeMP4Header()) {
        LOG(ERROR) << "[MP4Muxer] Failed to write MP4 header";
        return false;
    }
    
    m_initialized = true;
    LOG(INFO) << "[MP4Muxer] Initialized for " << width << "x" << height << " H264 recording";
    return true;
}

bool SimpleMP4Muxer::addFrame(const unsigned char* h264Data, size_t dataSize, bool isKeyFrame) {
    if (!m_initialized || !h264Data || dataSize == 0) {
        return false;
    }
    
    // Write length-prefixed H264 frame data
    write32(static_cast<uint32_t>(dataSize));
    writeBytes(h264Data, dataSize);
    
    m_frameCount++;
    if (isKeyFrame) {
        m_keyFrameCount++;
    }
    
    LOG(DEBUG) << "[MP4Muxer] Added frame " << m_frameCount << " (" << dataSize << " bytes" 
               << (isKeyFrame ? ", keyframe" : "") << ")";
    
    return true;
}

bool SimpleMP4Muxer::finalize() {
    if (!m_initialized) {
        return false;
    }
    
    // Update mdat box size
    if (!updateMdatSize()) {
        LOG(ERROR) << "[MP4Muxer] Failed to update mdat size";
        return false;
    }
    
    m_initialized = false;
    LOG(INFO) << "[MP4Muxer] Finalized MP4 file - " << m_frameCount << " frames (" 
              << m_keyFrameCount << " keyframes)";
    return true;
}

// Helper methods
void SimpleMP4Muxer::write32(uint32_t value) {
    uint8_t bytes[4] = {
        static_cast<uint8_t>((value >> 24) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>(value & 0xFF)
    };
    writeBytes(bytes, 4);
}

void SimpleMP4Muxer::write16(uint16_t value) {
    uint8_t bytes[2] = {
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>(value & 0xFF)
    };
    writeBytes(bytes, 2);
}

void SimpleMP4Muxer::write8(uint8_t value) {
    writeBytes(&value, 1);
}

void SimpleMP4Muxer::writeBytes(const void* data, size_t size) {
    if (write(m_fd, data, size) != static_cast<ssize_t>(size)) {
        LOG(ERROR) << "[MP4Muxer] Write failed: " << strerror(errno);
    }
    m_currentPos += size;
}

void SimpleMP4Muxer::writeString(const std::string& str) {
    writeBytes(str.data(), str.size());
}

bool SimpleMP4Muxer::writeMP4Header() {
    m_currentPos = 0;
    
    // Write ftyp box
    if (!writeFtypBox()) return false;
    
    // Write moov box (metadata)
    if (!writeMoovBox()) return false;
    
    // Start mdat box (video data)
    if (!startMdatBox()) return false;
    
    return true;
}

bool SimpleMP4Muxer::writeFtypBox() {
    write32(32); // box size
    writeString("ftyp");
    writeString("isom"); // major brand
    write32(0x200); // minor version
    writeString("isom"); // compatible brands
    writeString("iso2");
    writeString("avc1");
    writeString("mp41");
    
    return true;
}

bool SimpleMP4Muxer::writeMoovBox() {
    // This is a simplified moov box - for a real implementation,
    // we would need to write a complete movie header with proper timing
    
    size_t moovStart = m_currentPos;
    write32(0); // placeholder for size
    writeString("moov");
    
    // mvhd box (movie header)
    write32(108); // box size
    writeString("mvhd");
    write8(0); // version
    write8(0); write8(0); write8(0); // flags
    write32(0); // creation_time
    write32(0); // modification_time
    write32(1000); // timescale
    write32(1000); // duration (will be updated if needed)
    write32(0x00010000); // rate (1.0)
    write16(0x0100); // volume (1.0)
    write16(0); // reserved
    write32(0); write32(0); // reserved
    
    // transformation matrix (identity)
    write32(0x00010000); write32(0); write32(0);
    write32(0); write32(0x00010000); write32(0);
    write32(0); write32(0); write32(0x40000000);
    
    // pre_defined
    for (int i = 0; i < 6; i++) write32(0);
    write32(2); // next_track_ID
    
    // For simplicity, we're creating a minimal moov box
    // A complete implementation would need trak boxes with full metadata
    
    // Update moov box size
    size_t moovEnd = m_currentPos;
    size_t moovSize = moovEnd - moovStart;
    
    // Seek back and update size
    if (lseek(m_fd, moovStart, SEEK_SET) == -1) return false;
    write32(static_cast<uint32_t>(moovSize));
    if (lseek(m_fd, moovEnd, SEEK_SET) == -1) return false;
    m_currentPos = moovEnd;
    
    return true;
}

bool SimpleMP4Muxer::startMdatBox() {
    m_mdatStartPos = m_currentPos;
    write32(0); // placeholder for size - will be updated in finalize()
    writeString("mdat");
    
    return true;
}

bool SimpleMP4Muxer::updateMdatSize() {
    size_t currentPos = m_currentPos;
    size_t mdatSize = currentPos - m_mdatStartPos;
    
    // Seek back to mdat size field and update it
    if (lseek(m_fd, m_mdatStartPos, SEEK_SET) == -1) {
        LOG(ERROR) << "[MP4Muxer] Failed to seek to mdat size field: " << strerror(errno);
        return false;
    }
    
    write32(static_cast<uint32_t>(mdatSize));
    
    // Seek back to end of file
    if (lseek(m_fd, currentPos, SEEK_SET) == -1) {
        LOG(ERROR) << "[MP4Muxer] Failed to seek to end of file: " << strerror(errno);
        return false;
    }
    
    return true;
} 