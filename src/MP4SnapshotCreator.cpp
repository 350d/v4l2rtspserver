#include "MP4SnapshotCreator.h"
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <fstream>

void MP4SnapshotCreator::write32(std::vector<uint8_t>& vec, uint32_t value) {
    vec.push_back((value >> 24) & 0xFF);
    vec.push_back((value >> 16) & 0xFF);
    vec.push_back((value >> 8) & 0xFF);
    vec.push_back(value & 0xFF);
}

void MP4SnapshotCreator::write16(std::vector<uint8_t>& vec, uint16_t value) {
    vec.push_back((value >> 8) & 0xFF);
    vec.push_back(value & 0xFF);
}

void MP4SnapshotCreator::write8(std::vector<uint8_t>& vec, uint8_t value) {
    vec.push_back(value);
}

std::vector<uint8_t> MP4SnapshotCreator::createAvcCBox(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps) {
    std::vector<uint8_t> avcC;
    
    // avcC header
    write8(avcC, 1);          // configurationVersion
    write8(avcC, sps[1]);     // AVCProfileIndication (from SPS)
    write8(avcC, sps[2]);     // profile_compatibility (from SPS)
    write8(avcC, sps[3]);     // AVCLevelIndication (from SPS)
    write8(avcC, 0xFF);       // lengthSizeMinusOne (3 = 4 byte length)
    
    // SPS
    write8(avcC, 0xE1);       // numOfSequenceParameterSets (1 with reserved bits)
    write16(avcC, sps.size());
    avcC.insert(avcC.end(), sps.begin(), sps.end());
    
    // PPS
    write8(avcC, 1);          // numOfPictureParameterSets
    write16(avcC, pps.size());
    avcC.insert(avcC.end(), pps.begin(), pps.end());
    
    return avcC;
}

std::string MP4SnapshotCreator::generateCodecString(const std::vector<uint8_t>& sps) {
    if (sps.size() < 4) {
        return "avc1.640028"; // Fallback
    }
    
    std::ostringstream codecStr;
    codecStr << "avc1.";
    codecStr << std::hex << std::setfill('0') << std::setw(2) << (int)sps[1]; // profile_idc
    codecStr << std::hex << std::setfill('0') << std::setw(2) << (int)sps[2]; // constraint_set_flags
    codecStr << std::hex << std::setfill('0') << std::setw(2) << (int)sps[3]; // level_idc
    
    return codecStr.str();
}

MP4SnapshotCreator::MP4Data MP4SnapshotCreator::createSnapshot(const std::vector<uint8_t>& sps, 
                                                                const std::vector<uint8_t>& pps, 
                                                                const std::vector<uint8_t>& h264Frame,
                                                                uint16_t width, uint16_t height) {
    MP4Data result;
    
    if (sps.empty() || pps.empty() || h264Frame.empty()) {
        return result; // success = false
    }
    
    // SAFARI FIX: Create raw H.264 with start codes, then use system ffmpeg
    std::vector<uint8_t> rawH264;
    
    // Start code (0x00 0x00 0x00 0x01) + SPS
    rawH264.push_back(0x00); rawH264.push_back(0x00); 
    rawH264.push_back(0x00); rawH264.push_back(0x01);
    rawH264.insert(rawH264.end(), sps.begin(), sps.end());
    
    // Start code + PPS
    rawH264.push_back(0x00); rawH264.push_back(0x00); 
    rawH264.push_back(0x00); rawH264.push_back(0x01);
    rawH264.insert(rawH264.end(), pps.begin(), pps.end());
    
    // Start code + IDR frame
    rawH264.push_back(0x00); rawH264.push_back(0x00); 
    rawH264.push_back(0x00); rawH264.push_back(0x01);
    rawH264.insert(rawH264.end(), h264Frame.begin(), h264Frame.end());
    
    // Write temporary raw H.264 file
    std::string tempH264 = "/tmp/snapshot_" + std::to_string(time(nullptr)) + ".h264";
    std::string tempMP4 = "/tmp/snapshot_" + std::to_string(time(nullptr)) + ".mp4";
    
    std::ofstream h264File(tempH264, std::ios::binary);
    if (!h264File.is_open()) {
        return result; // success = false
    }
    
    h264File.write(reinterpret_cast<const char*>(rawH264.data()), rawH264.size());
    h264File.close();
    
    // Convert to MP4 using system ffmpeg
    std::string ffmpegCmd = "ffmpeg -f h264 -i " + tempH264 + 
                           " -c copy -movflags +faststart -f mp4 " + tempMP4 + 
                           " -y 2>/dev/null";
    
    int ret = system(ffmpegCmd.c_str());
    
    // Clean up temporary H.264 file
    remove(tempH264.c_str());
    
    if (ret != 0) {
        return result; // success = false
    }
    
    // Read generated MP4 file
    std::ifstream mp4File(tempMP4, std::ios::binary);
    if (!mp4File.is_open()) {
        return result; // success = false
    }
    
    result.data = std::vector<uint8_t>((std::istreambuf_iterator<char>(mp4File)),
                                       std::istreambuf_iterator<char>());
    mp4File.close();
    
    // Clean up temporary MP4 file
    remove(tempMP4.c_str());
    
    if (result.data.empty()) {
        return result; // success = false
    }
    
    // Generate proper MIME type with codec string
    std::string codecString = generateCodecString(sps);
    result.mimeType = "video/mp4; codecs=\"" + codecString + "\"";
    result.success = true;
    
    return result;
} 