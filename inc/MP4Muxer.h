#pragma once

#include <vector>
#include <string>
#include <cstdint>

class MP4Muxer {
private:
    int m_fd;
    std::vector<uint8_t> m_sps, m_pps;
    uint32_t m_frameCount;
    bool m_initialized;
    uint16_t m_width, m_height;
    
public:
    MP4Muxer(int fd, uint16_t width, uint16_t height);
    ~MP4Muxer();
    
    void setSPSPPS(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps);
    void writeKeyframe(const std::vector<uint8_t>& nalUnit);
    void finalize();
    
    bool isInitialized() const { return m_initialized; }
    uint32_t getFrameCount() const { return m_frameCount; }
}; 