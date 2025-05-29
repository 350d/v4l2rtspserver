#include "EnhancedAvcC.h"
#include <cstdio>

namespace SnapshotAvcC {

class BitReader {
private:
    const uint8_t* data;
    size_t size;
    size_t byte_pos;
    int bit_pos;
    
public:
    BitReader(const uint8_t* data, size_t size) 
        : data(data), size(size), byte_pos(0), bit_pos(0) {}
    
    bool hasMoreBits() const {
        return byte_pos < size;
    }
    
    uint32_t readBits(int n) {
        uint32_t result = 0;
        for (int i = 0; i < n && hasMoreBits(); i++) {
            int bit = (data[byte_pos] >> (7 - bit_pos)) & 1;
            result = (result << 1) | bit;
            
            bit_pos++;
            if (bit_pos == 8) {
                bit_pos = 0;
                byte_pos++;
            }
        }
        return result;
    }
    
    uint32_t readUE() {
        int leadingZeros = 0;
        while (hasMoreBits() && readBits(1) == 0) {
            leadingZeros++;
        }
        
        if (leadingZeros == 0) return 0;
        if (leadingZeros > 31) return 0; // Prevent overflow
        
        uint32_t value = readBits(leadingZeros);
        return (1U << leadingZeros) - 1 + value;
    }
};

SPSInfo::SPSInfo() : profile_idc(0x64), constraint_set_flags(0), level_idc(0x28), 
                     chroma_format_idc(1), bit_depth_luma_minus8(0), bit_depth_chroma_minus8(0),
                     valid(false) {}

SPSInfo parseBasicSPS(const std::string& sps) {
    SPSInfo info;
    
    if (sps.size() < 4) {
        return info; // Return defaults
    }
    
    // Extract basic profile/level info (always available)
    info.profile_idc = (uint8_t)sps[1];
    info.constraint_set_flags = (uint8_t)sps[2];
    info.level_idc = (uint8_t)sps[3];
    
    // Try to parse chroma format for High profiles
    if (sps.size() > 4 && (info.profile_idc == 100 || info.profile_idc == 110 || 
                          info.profile_idc == 122 || info.profile_idc == 244)) {
        try {
            BitReader reader((const uint8_t*)sps.data() + 1, sps.size() - 1);
            
            // Skip seq_parameter_set_id
            reader.readUE();
            
            // Read chroma_format_idc
            info.chroma_format_idc = reader.readUE();
            if (info.chroma_format_idc > 3) {
                info.chroma_format_idc = 1; // Fallback to 4:2:0
            }
            
            // Skip separate_colour_plane_flag if chroma_format_idc == 3
            if (info.chroma_format_idc == 3) {
                reader.readBits(1);
            }
            
            // Read bit depths
            info.bit_depth_luma_minus8 = reader.readUE();
            info.bit_depth_chroma_minus8 = reader.readUE();
            
            // Limit bit depths to reasonable values
            if (info.bit_depth_luma_minus8 > 6) info.bit_depth_luma_minus8 = 0;
            if (info.bit_depth_chroma_minus8 > 6) info.bit_depth_chroma_minus8 = 0;
            
        } catch (...) {
            // Use defaults if parsing fails
            info.chroma_format_idc = 1;
            info.bit_depth_luma_minus8 = 0;
            info.bit_depth_chroma_minus8 = 0;
        }
    }
    
    info.valid = true;
    return info;
}

std::string generateCodecString(const std::string& sps) {
    if (sps.size() < 4) {
        return "avc1.640028"; // Fallback: High Profile, Level 4.0
    }
    
    SPSInfo info = parseBasicSPS(sps);
    
    // Generate RFC 6381 compliant codec string
    char codecStr[32];
    snprintf(codecStr, sizeof(codecStr), "avc1.%02X%02X%02X", 
             info.profile_idc, info.constraint_set_flags, info.level_idc);
    
    return std::string(codecStr);
}

std::string getProfileName(uint8_t profile_idc) {
    switch (profile_idc) {
        case 66: return "Baseline";
        case 77: return "Main";
        case 88: return "Extended";
        case 100: return "High";
        case 110: return "High 10";
        case 122: return "High 4:2:2";
        case 244: return "High 4:4:4";
        default: return "Unknown";
    }
}

std::vector<uint8_t> createOptimizedAvcC(const std::string& sps, const std::string& pps) {
    std::vector<uint8_t> avcC;
    
    // Parse SPS for profile information
    SPSInfo spsInfo = parseBasicSPS(sps);
    
    // AVCDecoderConfigurationRecord structure:
    
    // configurationVersion (8 bits) = 1
    avcC.push_back(1);
    
    // AVCProfileIndication (8 bits)
    avcC.push_back(spsInfo.profile_idc);
    
    // profile_compatibility (8 bits) 
    avcC.push_back(spsInfo.constraint_set_flags);
    
    // AVCLevelIndication (8 bits)
    avcC.push_back(spsInfo.level_idc);
    
    // lengthSizeMinusOne (6 bits) + reserved (2 bits)
    // Use 4-byte length prefixes (NALU length = 4 bytes)
    avcC.push_back(0xFF); // 11111111 = reserved(111111) + lengthSizeMinusOne(11) = 4-1=3
    
    // numOfSequenceParameterSets (5 bits) + reserved (3 bits)
    avcC.push_back(0xE1); // 11100001 = reserved(111) + numSPS(00001) = 1
    
    // sequenceParameterSetLength (16 bits)
    uint16_t spsLength = sps.size();
    avcC.push_back((spsLength >> 8) & 0xFF);
    avcC.push_back(spsLength & 0xFF);
    
    // sequenceParameterSetNALUnit
    avcC.insert(avcC.end(), sps.begin(), sps.end());
    
    // numOfPictureParameterSets (8 bits)
    avcC.push_back(1);
    
    // pictureParameterSetLength (16 bits)
    uint16_t ppsLength = pps.size();
    avcC.push_back((ppsLength >> 8) & 0xFF);
    avcC.push_back(ppsLength & 0xFF);
    
    // pictureParameterSetNALUnit
    avcC.insert(avcC.end(), pps.begin(), pps.end());
    
    // Add High Profile extensions if needed
    if (spsInfo.profile_idc == 100 || spsInfo.profile_idc == 110 || 
        spsInfo.profile_idc == 122 || spsInfo.profile_idc == 244) {
        
        // reserved (6 bits) + chroma_format (2 bits)
        avcC.push_back(0xFC | (spsInfo.chroma_format_idc & 0x03));
        
        // reserved (5 bits) + bit_depth_luma_minus8 (3 bits)
        avcC.push_back(0xF8 | (spsInfo.bit_depth_luma_minus8 & 0x07));
        
        // reserved (5 bits) + bit_depth_chroma_minus8 (3 bits)
        avcC.push_back(0xF8 | (spsInfo.bit_depth_chroma_minus8 & 0x07));
        
        // numOfSequenceParameterSetExt (8 bits)
        avcC.push_back(0); // No SPS extensions
    }
    
    return avcC;
}

} // namespace SnapshotAvcC 