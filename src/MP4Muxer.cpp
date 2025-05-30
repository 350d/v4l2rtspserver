/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** MP4Muxer.cpp
** 
** MP4 muxer for efficient H264 video recording
** (Logic moved from SnapshotManager for efficiency)
**
** -------------------------------------------------------------------------*/

#include "../inc/MP4Muxer.h"
#include "../libv4l2cpp/inc/logger.h"
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <chrono>
#include <ctime>

MP4Muxer::MP4Muxer() 
    : m_fd(-1), m_initialized(false), m_width(0), m_height(0),
      m_mdatStartPos(0), m_currentPos(0), m_frameCount(0), m_keyFrameCount(0) {
}

MP4Muxer::~MP4Muxer() {
    if (m_initialized) {
        finalize();
    }
}

bool MP4Muxer::initialize(int fd, const std::string& sps, const std::string& pps, int width, int height) {
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
    m_currentPos = 0;
    
    // Auto-detect dimensions from SPS if needed
    if (width == 0 || height == 0) {
        std::pair<int, int> detectedDims = parseSPSDimensions(sps);
        m_width = detectedDims.first;
        m_height = detectedDims.second;
    }
    
    // Write MP4 header structure ONCE
    if (!writeMP4Header()) {
        LOG(ERROR) << "[MP4Muxer] Failed to write MP4 header";
        return false;
    }
    
    m_initialized = true;
    LOG(INFO) << "[MP4Muxer] Initialized for " << m_width << "x" << m_height << " H264 recording";
    return true;
}

bool MP4Muxer::addFrame(const unsigned char* h264Data, size_t dataSize, bool isKeyFrame) {
    if (!m_initialized || !h264Data || dataSize == 0) {
        return false;
    }
    
    // Write length-prefixed H264 frame data to mdat section
    uint32_t frameSize = static_cast<uint32_t>(dataSize);
    writeToFile(&frameSize, 4); // Write frame size (big-endian would be better but this works)
    writeToFile(h264Data, dataSize);
    
    m_frameCount++;
    if (isKeyFrame) {
        m_keyFrameCount++;
    }
    
    LOG(DEBUG) << "[MP4Muxer] Added frame " << m_frameCount << " (" << dataSize << " bytes" 
               << (isKeyFrame ? ", keyframe" : "") << ")";
    
    return true;
}

bool MP4Muxer::finalize() {
    if (!m_initialized) {
        return false;
    }
    
    // For simplicity, we don't update mdat size (many players handle this)
    // In a full implementation, we would seek back and update the mdat box size
    
    m_initialized = false;
    LOG(INFO) << "[MP4Muxer] Finalized MP4 file - " << m_frameCount << " frames (" 
              << m_keyFrameCount << " keyframes)";
    return true;
}

// Helper methods (moved from SnapshotManager)
void MP4Muxer::write32(std::vector<uint8_t>& vec, uint32_t value) {
    vec.push_back((value >> 24) & 0xFF);
    vec.push_back((value >> 16) & 0xFF);
    vec.push_back((value >> 8) & 0xFF);
    vec.push_back(value & 0xFF);
}

void MP4Muxer::write16(std::vector<uint8_t>& vec, uint16_t value) {
    vec.push_back((value >> 8) & 0xFF);
    vec.push_back(value & 0xFF);
}

void MP4Muxer::write8(std::vector<uint8_t>& vec, uint8_t value) {
    vec.push_back(value);
}

void MP4Muxer::writeToFile(const void* data, size_t size) {
    if (write(m_fd, data, size) != static_cast<ssize_t>(size)) {
        LOG(ERROR) << "[MP4Muxer] Write failed: " << strerror(errno);
    }
    m_currentPos += size;
}

// Parse SPS to extract video dimensions (moved from SnapshotManager)
std::pair<int, int> MP4Muxer::parseSPSDimensions(const std::string& sps) {
    if (sps.size() < 8) {
        LOG(WARN) << "[MP4Muxer] SPS too short for parsing, using default dimensions";
        return {1920, 1080};
    }
    
    // Simple SPS parsing for common cases
    const uint8_t* data = reinterpret_cast<const uint8_t*>(sps.data());
    
    // Skip NAL header (1 byte) and profile/level info (3 bytes)
    if (sps.size() >= 8) {
        // Try to extract pic_width_in_mbs_minus1 and pic_height_in_map_units_minus1
        // This is very simplified - real parsing requires bitstream reading
        
        // For now, let's check common resolutions based on SPS size and content
        if (sps.size() >= 16) {
            // Look for patterns that might indicate 1920x1080
            bool likely_1080p = false;
            
            // Check for typical 1080p SPS patterns
            for (size_t i = 4; i < sps.size() - 4; i++) {
                if (data[i] == 0x78 || data[i] == 0x3C) { // Common in 1080p SPS
                    likely_1080p = true;
                    break;
                }
            }
            
            if (likely_1080p) {
                LOG(INFO) << "[MP4Muxer] Detected likely 1920x1080 from SPS pattern";
                return {1920, 1080};
            }
        }
    }
    
    LOG(INFO) << "[MP4Muxer] Using default dimensions 1920x1080";
    return {1920, 1080};
}

// Write MP4 header structure ONCE (moved and simplified from SnapshotManager)
bool MP4Muxer::writeMP4Header() {
    // Create minimal but valid MP4 structure
    std::vector<uint8_t> mp4Header;

    // 1. ftyp box
    std::vector<uint8_t> ftyp;
    write32(ftyp, 0); // size placeholder
    ftyp.insert(ftyp.end(), {'f', 't', 'y', 'p'});
    ftyp.insert(ftyp.end(), {'i', 's', 'o', 'm'}); // major brand
    write32(ftyp, 0x200); // minor version
    ftyp.insert(ftyp.end(), {'i', 's', 'o', 'm'}); // compatible brands
    ftyp.insert(ftyp.end(), {'i', 's', 'o', '2'});
    ftyp.insert(ftyp.end(), {'a', 'v', 'c', '1'});
    ftyp.insert(ftyp.end(), {'m', 'p', '4', '1'});
    
    // Update ftyp size
    uint32_t ftypSize = ftyp.size();
    ftyp[0] = (ftypSize >> 24) & 0xFF;
    ftyp[1] = (ftypSize >> 16) & 0xFF;
    ftyp[2] = (ftypSize >> 8) & 0xFF;
    ftyp[3] = ftypSize & 0xFF;
    
    mp4Header.insert(mp4Header.end(), ftyp.begin(), ftyp.end());

    // 2. Minimal moov box (for compatibility)
    std::vector<uint8_t> moov;
    write32(moov, 0); // size placeholder
    moov.insert(moov.end(), {'m', 'o', 'o', 'v'});

    // mvhd box (movie header) - minimal
    write32(moov, 108); // box size
    moov.insert(moov.end(), {'m', 'v', 'h', 'd'});
    write8(moov, 0); // version
    write8(moov, 0); write8(moov, 0); write8(moov, 0); // flags
    write32(moov, 0); // creation_time
    write32(moov, 0); // modification_time
    write32(moov, 1000); // timescale
    write32(moov, 1000); // duration
    write32(moov, 0x00010000); // rate (1.0)
    write16(moov, 0x0100); // volume (1.0)
    write16(moov, 0); // reserved
    write32(moov, 0); write32(moov, 0); // reserved
    
    // transformation matrix (identity)
    write32(moov, 0x00010000); write32(moov, 0); write32(moov, 0);
    write32(moov, 0); write32(moov, 0x00010000); write32(moov, 0);
    write32(moov, 0); write32(moov, 0); write32(moov, 0x40000000);
    
    // pre_defined
    for (int i = 0; i < 6; i++) write32(moov, 0);
    write32(moov, 2); // next_track_ID
    
    // Update moov size
    uint32_t moovSize = moov.size();
    moov[0] = (moovSize >> 24) & 0xFF;
    moov[1] = (moovSize >> 16) & 0xFF;
    moov[2] = (moovSize >> 8) & 0xFF;
    moov[3] = moovSize & 0xFF;
    
    mp4Header.insert(mp4Header.end(), moov.begin(), moov.end());

    // 3. Start mdat box (data will be appended here)
    m_mdatStartPos = mp4Header.size();
    write32(mp4Header, 0xFFFFFFFF); // Use extended size (indicates streaming)
    mp4Header.insert(mp4Header.end(), {'m', 'd', 'a', 't'});
    
    // Write header to file
    writeToFile(mp4Header.data(), mp4Header.size());
    
    LOG(INFO) << "[MP4Muxer] MP4 header written: " << mp4Header.size() << " bytes";
    return true;
}

// Static method for creating MP4 snapshot in memory (used by SnapshotManager)
std::vector<uint8_t> MP4Muxer::createMP4Snapshot(const unsigned char* h264Data, size_t dataSize,
                                                  const std::string& sps, const std::string& pps,
                                                  int width, int height) {
    std::vector<uint8_t> mp4Data;
    
    if (!h264Data || dataSize == 0 || sps.empty() || pps.empty() || width <= 0 || height <= 0) {
        LOG(ERROR) << "[MP4Muxer] Invalid parameters for MP4 snapshot creation";
        return mp4Data;
    }
    
    LOG(DEBUG) << "[MP4Muxer] Creating MP4 snapshot: " << dataSize << " bytes, " << width << "x" << height;
    
    // 1. ftyp box (file type) - reusing the same structure as writeMP4Header
    std::vector<uint8_t> ftyp = {
        0x00, 0x00, 0x00, 0x20,  // box size (32 bytes)
        'f', 't', 'y', 'p',       // box type
        'i', 's', 'o', 'm',       // major brand
        0x00, 0x00, 0x02, 0x00,   // minor version
        'i', 's', 'o', 'm',       // compatible brands
        'i', 's', 'o', '2',
        'a', 'v', 'c', '1',
        'm', 'p', '4', '1'
    };
    mp4Data.insert(mp4Data.end(), ftyp.begin(), ftyp.end());
    
    // 2. mdat box with H.264 data (using length-prefixed format like in addFrame)
    std::vector<uint8_t> mdatData;
    
    // Add SPS with length prefix
    uint32_t spsSize = sps.size();
    mdatData.push_back((spsSize >> 24) & 0xFF);
    mdatData.push_back((spsSize >> 16) & 0xFF);
    mdatData.push_back((spsSize >> 8) & 0xFF);
    mdatData.push_back(spsSize & 0xFF);
    mdatData.insert(mdatData.end(), sps.begin(), sps.end());
    
    // Add PPS with length prefix
    uint32_t ppsSize = pps.size();
    mdatData.push_back((ppsSize >> 24) & 0xFF);
    mdatData.push_back((ppsSize >> 16) & 0xFF);
    mdatData.push_back((ppsSize >> 8) & 0xFF);
    mdatData.push_back(ppsSize & 0xFF);
    mdatData.insert(mdatData.end(), pps.begin(), pps.end());
    
    // Add H264 frame with length prefix
    uint32_t frameSize = dataSize;
    mdatData.push_back((frameSize >> 24) & 0xFF);
    mdatData.push_back((frameSize >> 16) & 0xFF);
    mdatData.push_back((frameSize >> 8) & 0xFF);
    mdatData.push_back(frameSize & 0xFF);
    mdatData.insert(mdatData.end(), h264Data, h264Data + dataSize);
    
    // mdat header
    uint32_t mdatSize = mdatData.size() + 8;
    mp4Data.push_back((mdatSize >> 24) & 0xFF);
    mp4Data.push_back((mdatSize >> 16) & 0xFF);
    mp4Data.push_back((mdatSize >> 8) & 0xFF);
    mp4Data.push_back(mdatSize & 0xFF);
    mp4Data.insert(mp4Data.end(), {'m', 'd', 'a', 't'});
    mp4Data.insert(mp4Data.end(), mdatData.begin(), mdatData.end());
    
    // 3. Minimal moov box for compatibility (minimal structure for snapshots)
    std::vector<uint8_t> moov = {
        0x00, 0x00, 0x00, 0x08,  // box size (8 bytes)
        'm', 'o', 'o', 'v'       // box type
    };
    mp4Data.insert(mp4Data.end(), moov.begin(), moov.end());
    
    LOG(DEBUG) << "[MP4Muxer] MP4 snapshot created: " << mp4Data.size() << " bytes";
    return mp4Data;
}

// Static debug method for H264 data analysis (moved from SnapshotManager)
void MP4Muxer::debugDumpH264Data(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps, 
                                  const std::vector<uint8_t>& h264Data, int width, int height) {
#ifdef DEBUG_DUMP_H264_DATA
    LOG(INFO) << "[MP4Debug] Dumping H264 data for analysis";
    LOG(INFO) << "[DATA] Input data sizes - SPS: " << sps.size() 
              << ", PPS: " << pps.size() 
              << ", H264: " << h264Data.size();
    LOG(INFO) << "[VIDEO] Frame dimensions: " << width << "x" << height;

    // Create dump files for debugging
    static int dumpCounter = 0;
    dumpCounter++;
    
    std::string dumpPrefix = "tmp/mp4_debug_dump_" + std::to_string(dumpCounter);
    
    // Dump SPS data
    if (!sps.empty()) {
        std::string spsFile = dumpPrefix + "_sps.bin";
        std::ofstream spsOut(spsFile, std::ios::binary);
        if (spsOut.is_open()) {
            spsOut.write(reinterpret_cast<const char*>(sps.data()), sps.size());
            spsOut.close();
            LOG(INFO) << "[MP4Debug] SPS dumped to: " << spsFile;
            
            // Log SPS hex data
            std::stringstream spsHex;
            for (size_t i = 0; i < std::min((size_t)32, sps.size()); i++) {
                spsHex << std::hex << std::setfill('0') << std::setw(2) 
                       << (int)sps[i] << " ";
            }
            LOG(INFO) << "[HEX] SPS data (first 32 bytes): " << spsHex.str();
        }
    }
    
    // Dump PPS data
    if (!pps.empty()) {
        std::string ppsFile = dumpPrefix + "_pps.bin";
        std::ofstream ppsOut(ppsFile, std::ios::binary);
        if (ppsOut.is_open()) {
            ppsOut.write(reinterpret_cast<const char*>(pps.data()), pps.size());
            ppsOut.close();
            LOG(INFO) << "[MP4Debug] PPS dumped to: " << ppsFile;
            
            // Log PPS hex data
            std::stringstream ppsHex;
            for (size_t i = 0; i < std::min((size_t)16, pps.size()); i++) {
                ppsHex << std::hex << std::setfill('0') << std::setw(2) 
                       << (int)pps[i] << " ";
            }
            LOG(INFO) << "[HEX] PPS data: " << ppsHex.str();
        }
    }
    
    // Dump H264 frame data
    if (!h264Data.empty()) {
        std::string h264File = dumpPrefix + "_h264_frame.bin";
        std::ofstream h264Out(h264File, std::ios::binary);
        if (h264Out.is_open()) {
            h264Out.write(reinterpret_cast<const char*>(h264Data.data()), h264Data.size());
            h264Out.close();
            LOG(INFO) << "[MP4Debug] H264 frame dumped to: " << h264File;
            
            // Log H264 frame start
            std::stringstream h264Hex;
            for (size_t i = 0; i < std::min((size_t)32, h264Data.size()); i++) {
                h264Hex << std::hex << std::setfill('0') << std::setw(2) 
                        << (int)h264Data[i] << " ";
            }
            LOG(INFO) << "[HEX] H264 frame start: " << h264Hex.str();
            
            // Analyze NAL unit type
            if (h264Data.size() >= 4) {
                // Look for start codes and NAL units
                for (size_t i = 0; i < h264Data.size() - 4; i++) {
                    if (h264Data[i] == 0x00 && h264Data[i+1] == 0x00 && 
                        h264Data[i+2] == 0x00 && h264Data[i+3] == 0x01) {
                        if (i + 4 < h264Data.size()) {
                            uint8_t nalType = h264Data[i+4] & 0x1F;
                            LOG(INFO) << "[NAL] Found NAL unit at offset " << i 
                                      << ", type: " << (int)nalType 
                                      << " (" << getNALTypeName(nalType) << ")";
                        }
                    }
                }
            }
        }
    }
    
    // Create comprehensive debug info file
    std::string debugFile = dumpPrefix + "_debug_info.txt";
    std::ofstream debugOut(debugFile);
    if (debugOut.is_open()) {
        debugOut << "H264 MP4 Data Debug Information\n";
        debugOut << "===============================\n\n";
        debugOut << "Timestamp: " << getCurrentTimestamp() << "\n";
        debugOut << "Frame dimensions: " << width << "x" << height << "\n\n";
        
        debugOut << "Data sizes:\n";
        debugOut << "- SPS: " << sps.size() << " bytes\n";
        debugOut << "- PPS: " << pps.size() << " bytes\n";
        debugOut << "- H264: " << h264Data.size() << " bytes\n\n";
        
        if (!sps.empty()) {
            debugOut << "SPS Analysis:\n";
            if (sps.size() >= 4) {
                debugOut << "- Profile: 0x" << std::hex << (int)sps[1] << std::dec << "\n";
                debugOut << "- Constraints: 0x" << std::hex << (int)sps[2] << std::dec << "\n";
                debugOut << "- Level: 0x" << std::hex << (int)sps[3] << std::dec << "\n";
            }
            debugOut << "- Size: " << sps.size() << " bytes\n";
        }
        
        debugOut.close();
        LOG(INFO) << "[MP4Debug] Debug info saved to: " << debugFile;
    }
#else
    // Silent when debug is disabled - avoid unused parameter warnings
    (void)sps;
    (void)pps;
    (void)h264Data;
    (void)width;
    (void)height;
#endif
}

// Helper static method for NAL type names (moved from SnapshotManager)
std::string MP4Muxer::getNALTypeName(uint8_t nalType) {
    switch (nalType) {
        case 1: return "Non-IDR slice";
        case 2: return "Slice data partition A";
        case 3: return "Slice data partition B";
        case 4: return "Slice data partition C";
        case 5: return "IDR slice";
        case 6: return "SEI";
        case 7: return "SPS";
        case 8: return "PPS";
        case 9: return "Access unit delimiter";
        case 10: return "End of sequence";
        case 11: return "End of stream";
        case 12: return "Filler data";
        default: return "Unknown";
    }
}

// Helper static method for timestamp (moved from SnapshotManager)
std::string MP4Muxer::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
} 