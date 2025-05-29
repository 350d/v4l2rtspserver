#include "MP4Muxer.h"
#include "MP4SnapshotCreator.h"
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
    
    // Use existing MP4SnapshotCreator to create complete MP4 for this keyframe
    auto mp4Data = MP4SnapshotCreator::createSnapshot(m_sps, m_pps, nalUnit, m_width, m_height);
    
    if (!mp4Data.success || mp4Data.data.empty()) {
        LOG(ERROR) << "[MP4MUXER] Failed to create MP4 snapshot";
        return;
    }
    
    // Write MP4 data to file
    ssize_t written = write(m_fd, mp4Data.data.data(), mp4Data.data.size());
    if (written != (ssize_t)mp4Data.data.size()) {
        LOG(ERROR) << "[MP4MUXER] Write error: " << written << "/" << mp4Data.data.size() 
                   << " err: " << strerror(errno);
        return;
    }
    
    m_frameCount++;
    LOG(INFO) << "[MP4MUXER] Keyframe #" << m_frameCount << " written: " 
              << mp4Data.data.size() << " bytes (NAL: " << nalUnit.size() << ")";
}

void MP4Muxer::finalize() {
    if (m_frameCount > 0) {
        LOG(INFO) << "[MP4MUXER] Finalized with " << m_frameCount << " keyframes";
    }
} 