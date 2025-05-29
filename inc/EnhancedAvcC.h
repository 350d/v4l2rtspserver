#ifndef ENHANCED_AVCC_H
#define ENHANCED_AVCC_H

#include <vector>
#include <string>
#include <cstdint>

namespace SnapshotAvcC {

struct SPSInfo {
    uint8_t profile_idc;
    uint8_t constraint_set_flags;
    uint8_t level_idc;
    uint8_t chroma_format_idc;
    uint8_t bit_depth_luma_minus8;
    uint8_t bit_depth_chroma_minus8;
    bool valid;
    
    SPSInfo();
};

// Parse basic SPS information for avcC creation
SPSInfo parseBasicSPS(const std::string& sps);

// Generate RFC 6381 compliant codec string
std::string generateCodecString(const std::string& sps);

// Get profile name for logging
std::string getProfileName(uint8_t profile_idc);

// Create optimized avcC configuration data (without box header)
std::vector<uint8_t> createOptimizedAvcC(const std::string& sps, const std::string& pps);

} // namespace SnapshotAvcC

#endif // ENHANCED_AVCC_H 