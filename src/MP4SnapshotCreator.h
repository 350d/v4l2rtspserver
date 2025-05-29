#pragma once

#include <vector>
#include <string>
#include <cstdint>

class MP4SnapshotCreator {
public:
    struct MP4Data {
        std::vector<uint8_t> data;
        std::string mimeType;
        bool success;
        
        MP4Data() : success(false) {}
    };
    
    static MP4Data createSnapshot(const std::vector<uint8_t>& sps, 
                                  const std::vector<uint8_t>& pps, 
                                  const std::vector<uint8_t>& h264Frame,
                                  uint16_t width = 1920, uint16_t height = 1080);

private:
    static void write32(std::vector<uint8_t>& vec, uint32_t value);
    static void write16(std::vector<uint8_t>& vec, uint16_t value);
    static void write8(std::vector<uint8_t>& vec, uint8_t value);
    static std::vector<uint8_t> createAvcCBox(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps);
    static std::string generateCodecString(const std::vector<uint8_t>& sps);
}; 