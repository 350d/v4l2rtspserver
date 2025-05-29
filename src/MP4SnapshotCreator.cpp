#include "MP4SnapshotCreator.h"
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <fstream>
#include <iostream>

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
    
    std::vector<uint8_t> mp4Data;
    
    // Create ftyp box (file type)
    uint32_t ftypSize = 20;
    write32(mp4Data, ftypSize);
    mp4Data.insert(mp4Data.end(), {'f', 't', 'y', 'p'}); // box type
    mp4Data.insert(mp4Data.end(), {'i', 's', 'o', 'm'}); // major brand
    write32(mp4Data, 512);                               // minor version
    mp4Data.insert(mp4Data.end(), {'i', 's', 'o', 'm'}); // compatible brand
    
    // Create mdat box (media data) - placeholder, will update size later
    size_t mdatSizePos = mp4Data.size();
    write32(mp4Data, 0); // placeholder for size
    mp4Data.insert(mp4Data.end(), {'m', 'd', 'a', 't'});
    
    size_t mdatDataStart = mp4Data.size();
    
    // Add H.264 data with length prefixes (MP4 format)
    // Convert Annex B start codes to length prefixes
    
    // SPS with length prefix
    write32(mp4Data, sps.size());
    mp4Data.insert(mp4Data.end(), sps.begin(), sps.end());
    
    // PPS with length prefix
    write32(mp4Data, pps.size());
    mp4Data.insert(mp4Data.end(), pps.begin(), pps.end());
    
    // IDR frame with length prefix
    write32(mp4Data, h264Frame.size());
    mp4Data.insert(mp4Data.end(), h264Frame.begin(), h264Frame.end());
    
    // Update mdat size
    uint32_t mdatSize = mp4Data.size() - mdatSizePos;
    mp4Data[mdatSizePos] = (mdatSize >> 24) & 0xFF;
    mp4Data[mdatSizePos + 1] = (mdatSize >> 16) & 0xFF;
    mp4Data[mdatSizePos + 2] = (mdatSize >> 8) & 0xFF;
    mp4Data[mdatSizePos + 3] = mdatSize & 0xFF;
    
    // Create moov box (movie metadata)
    std::vector<uint8_t> moovBox;
    
    // Create mvhd box (movie header)
    std::vector<uint8_t> mvhdBox;
    write32(mvhdBox, 108); // box size
    mvhdBox.insert(mvhdBox.end(), {'m', 'v', 'h', 'd'});
    write8(mvhdBox, 0);   // version
    write8(mvhdBox, 0); write8(mvhdBox, 0); write8(mvhdBox, 0); // flags
    write32(mvhdBox, 0);  // creation time
    write32(mvhdBox, 0);  // modification time
    write32(mvhdBox, 1000); // timescale
    write32(mvhdBox, 1000); // duration
    write32(mvhdBox, 0x00010000); // rate
    write16(mvhdBox, 0x0100);     // volume
    write16(mvhdBox, 0);          // reserved
    write32(mvhdBox, 0); write32(mvhdBox, 0); // reserved
    // Matrix (identity)
    write32(mvhdBox, 0x00010000); write32(mvhdBox, 0); write32(mvhdBox, 0);
    write32(mvhdBox, 0); write32(mvhdBox, 0x00010000); write32(mvhdBox, 0);
    write32(mvhdBox, 0); write32(mvhdBox, 0); write32(mvhdBox, 0x40000000);
    // Predefined
    for (int i = 0; i < 6; i++) write32(mvhdBox, 0);
    write32(mvhdBox, 2); // next track ID
    
    // Create trak box (track)
    std::vector<uint8_t> trakBox;
    
    // Create tkhd box (track header)
    std::vector<uint8_t> tkhdBox;
    write32(tkhdBox, 92); // box size
    tkhdBox.insert(tkhdBox.end(), {'t', 'k', 'h', 'd'});
    write8(tkhdBox, 0);   // version
    write8(tkhdBox, 0); write8(tkhdBox, 0); write8(tkhdBox, 7); // flags (enabled, in movie, in preview)
    write32(tkhdBox, 0);  // creation time
    write32(tkhdBox, 0);  // modification time
    write32(tkhdBox, 1);  // track ID
    write32(tkhdBox, 0);  // reserved
    write32(tkhdBox, 1000); // duration
    write32(tkhdBox, 0); write32(tkhdBox, 0); // reserved
    write16(tkhdBox, 0);  // layer
    write16(tkhdBox, 0);  // alternate group
    write16(tkhdBox, 0);  // volume
    write16(tkhdBox, 0);  // reserved
    // Matrix (identity)
    write32(tkhdBox, 0x00010000); write32(tkhdBox, 0); write32(tkhdBox, 0);
    write32(tkhdBox, 0); write32(tkhdBox, 0x00010000); write32(tkhdBox, 0);
    write32(tkhdBox, 0); write32(tkhdBox, 0); write32(tkhdBox, 0x40000000);
    write32(tkhdBox, width << 16);  // width
    write32(tkhdBox, height << 16); // height
    
    // Create mdia box (media)
    std::vector<uint8_t> mdiaBox;
    
    // Create mdhd box (media header)
    std::vector<uint8_t> mdhdBox;
    write32(mdhdBox, 32); // box size
    mdhdBox.insert(mdhdBox.end(), {'m', 'd', 'h', 'd'});
    write8(mdhdBox, 0);   // version
    write8(mdhdBox, 0); write8(mdhdBox, 0); write8(mdhdBox, 0); // flags
    write32(mdhdBox, 0);  // creation time
    write32(mdhdBox, 0);  // modification time
    write32(mdhdBox, 1000); // timescale
    write32(mdhdBox, 1000); // duration
    write16(mdhdBox, 0x55c4); // language (und)
    write16(mdhdBox, 0);      // predefined
    
    // Create hdlr box (handler)
    std::vector<uint8_t> hdlrBox;
    write32(hdlrBox, 45); // box size
    hdlrBox.insert(hdlrBox.end(), {'h', 'd', 'l', 'r'});
    write8(hdlrBox, 0);   // version
    write8(hdlrBox, 0); write8(hdlrBox, 0); write8(hdlrBox, 0); // flags
    write32(hdlrBox, 0);  // predefined
    hdlrBox.insert(hdlrBox.end(), {'v', 'i', 'd', 'e'}); // handler type
    write32(hdlrBox, 0); write32(hdlrBox, 0); write32(hdlrBox, 0); // reserved
    hdlrBox.insert(hdlrBox.end(), {'V', 'i', 'd', 'e', 'o', 'H', 'a', 'n', 'd', 'l', 'e', 'r', 0}); // name
    
    // Create minf box (media information)
    std::vector<uint8_t> minfBox;
    
    // Create vmhd box (video media header)
    std::vector<uint8_t> vmhdBox;
    write32(vmhdBox, 20); // box size
    vmhdBox.insert(vmhdBox.end(), {'v', 'm', 'h', 'd'});
    write8(vmhdBox, 0);   // version
    write8(vmhdBox, 0); write8(vmhdBox, 0); write8(vmhdBox, 1); // flags
    write16(vmhdBox, 0);  // graphics mode
    write16(vmhdBox, 0); write16(vmhdBox, 0); write16(vmhdBox, 0); // opcolor
    
    // Create dinf box (data information)
    std::vector<uint8_t> dinfBox;
    write32(dinfBox, 36); // box size
    dinfBox.insert(dinfBox.end(), {'d', 'i', 'n', 'f'});
    
    // Create dref box (data reference)
    write32(dinfBox, 28); // box size
    dinfBox.insert(dinfBox.end(), {'d', 'r', 'e', 'f'});
    write8(dinfBox, 0);   // version
    write8(dinfBox, 0); write8(dinfBox, 0); write8(dinfBox, 0); // flags
    write32(dinfBox, 1);  // entry count
    
    // Create url box
    write32(dinfBox, 12); // box size
    dinfBox.insert(dinfBox.end(), {'u', 'r', 'l', ' '});
    write8(dinfBox, 0);   // version
    write8(dinfBox, 0); write8(dinfBox, 0); write8(dinfBox, 1); // flags (self-contained)
    
    // Create stbl box (sample table)
    std::vector<uint8_t> stblBox;
    
    // Create stsd box (sample description)
    std::vector<uint8_t> stsdBox;
    write32(stsdBox, 16 + 86); // box size (will be updated)
    stsdBox.insert(stsdBox.end(), {'s', 't', 's', 'd'});
    write8(stsdBox, 0);   // version
    write8(stsdBox, 0); write8(stsdBox, 0); write8(stsdBox, 0); // flags
    write32(stsdBox, 1);  // entry count
    
    // Create avc1 box (AVC sample entry)
    write32(stsdBox, 86); // box size
    stsdBox.insert(stsdBox.end(), {'a', 'v', 'c', '1'});
    for (int i = 0; i < 6; i++) write8(stsdBox, 0); // reserved
    write16(stsdBox, 1);  // data reference index
    write16(stsdBox, 0);  // predefined
    write16(stsdBox, 0);  // reserved
    for (int i = 0; i < 3; i++) write32(stsdBox, 0); // predefined
    write16(stsdBox, width);  // width
    write16(stsdBox, height); // height
    write32(stsdBox, 0x00480000); // horizontal resolution 72 dpi
    write32(stsdBox, 0x00480000); // vertical resolution 72 dpi
    write32(stsdBox, 0);  // reserved
    write16(stsdBox, 1);  // frame count
    for (int i = 0; i < 32; i++) write8(stsdBox, 0); // compressor name
    write16(stsdBox, 24); // depth
    write16(stsdBox, 0xFFFF); // predefined
    
    // Create avcC box
    auto avcCData = createAvcCBox(sps, pps);
    write32(stsdBox, 8 + avcCData.size()); // avcC box size
    stsdBox.insert(stsdBox.end(), {'a', 'v', 'c', 'C'});
    stsdBox.insert(stsdBox.end(), avcCData.begin(), avcCData.end());
    
    // Update stsd size
    uint32_t stsdSize = stsdBox.size();
    stsdBox[0] = (stsdSize >> 24) & 0xFF;
    stsdBox[1] = (stsdSize >> 16) & 0xFF;
    stsdBox[2] = (stsdSize >> 8) & 0xFF;
    stsdBox[3] = stsdSize & 0xFF;
    
    // Create stts box (time to sample)
    std::vector<uint8_t> sttsBox;
    write32(sttsBox, 24); // box size
    sttsBox.insert(sttsBox.end(), {'s', 't', 't', 's'});
    write8(sttsBox, 0);   // version
    write8(sttsBox, 0); write8(sttsBox, 0); write8(sttsBox, 0); // flags
    write32(sttsBox, 1);  // entry count
    write32(sttsBox, 1);  // sample count
    write32(sttsBox, 1000); // sample duration
    
    // Create stsc box (sample to chunk)
    std::vector<uint8_t> stscBox;
    write32(stscBox, 28); // box size
    stscBox.insert(stscBox.end(), {'s', 't', 's', 'c'});
    write8(stscBox, 0);   // version
    write8(stscBox, 0); write8(stscBox, 0); write8(stscBox, 0); // flags
    write32(stscBox, 1);  // entry count
    write32(stscBox, 1);  // first chunk
    write32(stscBox, 3);  // samples per chunk (SPS + PPS + IDR)
    write32(stscBox, 1);  // sample description index
    
    // Create stsz box (sample sizes)
    std::vector<uint8_t> stszBox;
    write32(stszBox, 32); // box size
    stszBox.insert(stszBox.end(), {'s', 't', 's', 'z'});
    write8(stszBox, 0);   // version
    write8(stszBox, 0); write8(stszBox, 0); write8(stszBox, 0); // flags
    write32(stszBox, 0);  // sample size (0 = variable)
    write32(stszBox, 3);  // sample count
    write32(stszBox, sps.size() + 4);      // SPS size + length prefix
    write32(stszBox, pps.size() + 4);      // PPS size + length prefix
    write32(stszBox, h264Frame.size() + 4); // IDR size + length prefix
    
    // Create stco box (chunk offsets)
    std::vector<uint8_t> stcoBox;
    write32(stcoBox, 20); // box size
    stcoBox.insert(stcoBox.end(), {'s', 't', 'c', 'o'});
    write8(stcoBox, 0);   // version
    write8(stcoBox, 0); write8(stcoBox, 0); write8(stcoBox, 0); // flags
    write32(stcoBox, 1);  // entry count
    write32(stcoBox, mdatDataStart); // chunk offset
    
    // Assemble stbl box
    uint32_t stblSize = 8 + stsdBox.size() + sttsBox.size() + stscBox.size() + stszBox.size() + stcoBox.size();
    write32(stblBox, stblSize);
    stblBox.insert(stblBox.end(), {'s', 't', 'b', 'l'});
    stblBox.insert(stblBox.end(), stsdBox.begin(), stsdBox.end());
    stblBox.insert(stblBox.end(), sttsBox.begin(), sttsBox.end());
    stblBox.insert(stblBox.end(), stscBox.begin(), stscBox.end());
    stblBox.insert(stblBox.end(), stszBox.begin(), stszBox.end());
    stblBox.insert(stblBox.end(), stcoBox.begin(), stcoBox.end());
    
    // Assemble minf box
    uint32_t minfSize = 8 + vmhdBox.size() + dinfBox.size() + stblBox.size();
    write32(minfBox, minfSize);
    minfBox.insert(minfBox.end(), {'m', 'i', 'n', 'f'});
    minfBox.insert(minfBox.end(), vmhdBox.begin(), vmhdBox.end());
    minfBox.insert(minfBox.end(), dinfBox.begin(), dinfBox.end());
    minfBox.insert(minfBox.end(), stblBox.begin(), stblBox.end());
    
    // Assemble mdia box
    uint32_t mdiaSize = 8 + mdhdBox.size() + hdlrBox.size() + minfBox.size();
    write32(mdiaBox, mdiaSize);
    mdiaBox.insert(mdiaBox.end(), {'m', 'd', 'i', 'a'});
    mdiaBox.insert(mdiaBox.end(), mdhdBox.begin(), mdhdBox.end());
    mdiaBox.insert(mdiaBox.end(), hdlrBox.begin(), hdlrBox.end());
    mdiaBox.insert(mdiaBox.end(), minfBox.begin(), minfBox.end());
    
    // Assemble trak box
    uint32_t trakSize = 8 + tkhdBox.size() + mdiaBox.size();
    write32(trakBox, trakSize);
    trakBox.insert(trakBox.end(), {'t', 'r', 'a', 'k'});
    trakBox.insert(trakBox.end(), tkhdBox.begin(), tkhdBox.end());
    trakBox.insert(trakBox.end(), mdiaBox.begin(), mdiaBox.end());
    
    // Assemble moov box
    uint32_t moovSize = 8 + mvhdBox.size() + trakBox.size();
    write32(moovBox, moovSize);
    moovBox.insert(moovBox.end(), {'m', 'o', 'o', 'v'});
    moovBox.insert(moovBox.end(), mvhdBox.begin(), mvhdBox.end());
    moovBox.insert(moovBox.end(), trakBox.begin(), trakBox.end());
    
    // Add moov box to MP4 data
    mp4Data.insert(mp4Data.end(), moovBox.begin(), moovBox.end());
    
    result.data = mp4Data;
    result.mimeType = "video/mp4; codecs=\"" + generateCodecString(sps) + "\"";
    result.success = true;
    
    return result;
} 