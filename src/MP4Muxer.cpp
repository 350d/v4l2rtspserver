#include "MP4Muxer.h"
#include "logger.h"
#include <unistd.h>
#include <cstring>

MP4Muxer::MP4Muxer(int fd, uint16_t width, uint16_t height) 
    : m_fd(fd), m_frameCount(0), m_initialized(false), m_width(width), m_height(height) {
    LOG(INFO) << "[MP4MUXER] Initialized for " << width << "x" << height;
}

MP4Muxer::~MP4Muxer() {
    finalize();
}

void MP4Muxer::setSPSPPS(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps) {
    if (sps.empty() || pps.empty()) {
        LOG(WARN) << "[MP4MUXER] Empty SPS or PPS provided";
        return;
    }
    
    m_sps = sps;
    m_pps = pps;
    m_initialized = true;
    
    LOG(INFO) << "[MP4MUXER] SPS/PPS set: " << sps.size() << "/" << pps.size() << " bytes";
}

void MP4Muxer::writeKeyframe(const std::vector<uint8_t>& nalUnit) {
    if (!m_initialized || nalUnit.empty()) {
        LOG(DEBUG) << "[MP4MUXER] Not ready or empty frame, skipping";
        return;
    }
    
    if (m_fd == -1) {
        LOG(ERROR) << "[MP4MUXER] Invalid file descriptor";
        return;
    }
    
    // Create simple MP4 with ftyp + mdat + moov boxes (using same algorithm as SnapshotManager)
    std::vector<uint8_t> mp4Data;

    // ftyp box
    const uint8_t ftypBox[] = {
        0x00, 0x00, 0x00, 0x20,             // box size
        0x66, 0x74, 0x79, 0x70,             // 'ftyp'
        0x69, 0x73, 0x6F, 0x6D,             // major_brand: 'isom'
        0x00, 0x00, 0x02, 0x00,             // minor_version: 0x200
        0x69, 0x73, 0x6F, 0x6D,             // compatible_brands: 'isom'
        0x69, 0x73, 0x6F, 0x32,             // 'iso2'
        0x61, 0x76, 0x63, 0x31,             // 'avc1'
        0x6D, 0x70, 0x34, 0x31              // 'mp41'
    };
    mp4Data.insert(mp4Data.end(), ftypBox, ftypBox + sizeof(ftypBox));

    // mdat box - contains NAL units with start codes
    std::vector<uint8_t> mdatData;
    const uint8_t startCode[] = {0x00, 0x00, 0x00, 0x01};

    // Add SPS
    if (!m_sps.empty()) {
        mdatData.insert(mdatData.end(), startCode, startCode + 4);
        mdatData.insert(mdatData.end(), m_sps.begin(), m_sps.end());
    }

    // Add PPS
    if (!m_pps.empty()) {
        mdatData.insert(mdatData.end(), startCode, startCode + 4);
        mdatData.insert(mdatData.end(), m_pps.begin(), m_pps.end());
    }

    // Add H264 frame
    mdatData.insert(mdatData.end(), startCode, startCode + 4);
    mdatData.insert(mdatData.end(), nalUnit.begin(), nalUnit.end());

    // mdat box header
    uint32_t mdatSize = mdatData.size() + 8;
    mp4Data.push_back((mdatSize >> 24) & 0xFF);
    mp4Data.push_back((mdatSize >> 16) & 0xFF);
    mp4Data.push_back((mdatSize >> 8) & 0xFF);
    mp4Data.push_back(mdatSize & 0xFF);
    mp4Data.push_back('m');
    mp4Data.push_back('d');
    mp4Data.push_back('a');
    mp4Data.push_back('t');
    mp4Data.insert(mp4Data.end(), mdatData.begin(), mdatData.end());

    // Minimal moov box for compatibility
    const uint8_t moovBox[] = {
        0x00, 0x00, 0x00, 0x08,             // box size
        0x6D, 0x6F, 0x6F, 0x76              // 'moov'
    };
    mp4Data.insert(mp4Data.end(), moovBox, moovBox + sizeof(moovBox));

    // Write MP4 data to file
    ssize_t written = write(m_fd, mp4Data.data(), mp4Data.size());
    if (written != (ssize_t)mp4Data.size()) {
        LOG(ERROR) << "[MP4MUXER] Write error: " << written << "/" << mp4Data.size() 
                   << " err: " << strerror(errno);
        return;
    }
    
    m_frameCount++;
    LOG(INFO) << "[MP4MUXER] Keyframe #" << m_frameCount << " written: " 
              << mp4Data.size() << " bytes (NAL: " << nalUnit.size() << ")";
}

void MP4Muxer::finalize() {
    if (m_frameCount > 0) {
        LOG(INFO) << "[MP4MUXER] Finalized with " << m_frameCount << " keyframes";
    }
} 