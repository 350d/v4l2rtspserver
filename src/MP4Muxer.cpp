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
    m_frames.clear(); // initialize empty frame vector
}

MP4Muxer::~MP4Muxer() {
    if (m_initialized) {
        finalize();
    }
}

bool MP4Muxer::initialize(int fd, const std::string& sps, const std::string& pps, int width, int height, int fps) {
    if (fd < 0 || sps.empty() || pps.empty() || width <= 0 || height <= 0 || fps <= 0) {
        LOG(ERROR) << "[MP4Muxer] Invalid initialization parameters";
        return false;
    }
    
    m_fd = fd;
    m_sps = sps;
    m_pps = pps;
    m_width = width;
    m_height = height;
    m_fps = fps;  // Store FPS parameter
    m_frameCount = 0;
    m_keyFrameCount = 0;
    m_currentPos = 0;
    m_frames.clear(); // clear frame information
    
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
    
    // FIXED version: don't add SPS/PPS to each frame for streaming
    // Write only H264 frame data with length prefix
    uint32_t frameSize = static_cast<uint32_t>(dataSize);
    
    // Big-endian format for MP4 compatibility
    uint8_t lenBytes[4];
    lenBytes[0] = (frameSize >> 24) & 0xFF;
    lenBytes[1] = (frameSize >> 16) & 0xFF;
    lenBytes[2] = (frameSize >> 8) & 0xFF;
    lenBytes[3] = frameSize & 0xFF;
    
    writeToFile(lenBytes, 4); // Write frame size in big-endian
    writeToFile(h264Data, dataSize);
    
    // Save metadata for possible moov writing at the end
    FrameInfo frameInfo;
    frameInfo.size = dataSize + 4; // frame size + length prefix
    frameInfo.isKeyFrame = isKeyFrame;
    frameInfo.offset = m_currentPos - frameInfo.size; // position in file
    m_frames.push_back(frameInfo);
    
    m_frameCount++;
    if (isKeyFrame) {
        m_keyFrameCount++;
    }
    
    LOG(DEBUG) << "[MP4Muxer] Added frame " << m_frameCount << " (" << dataSize << " bytes" 
               << (isKeyFrame ? ", keyframe" : "") << ") at offset " << frameInfo.offset;
    
    return true;
}

bool MP4Muxer::finalize() {
    if (!m_initialized) {
        return false;
    }
    
    // CRITICAL: Force data to disk immediately
    if (m_fd != -1) {
        fsync(m_fd);
        LOG(INFO) << "[MP4Muxer] Forced data sync to disk";
    }
    
    // FIXED version: create proper moov box at the end of file
    if (m_frameCount > 1 && !m_frames.empty()) {
        // Create proper moov box for multiple frames like in createFixedMultiFrameMP4
        std::vector<uint8_t> finalMoov = createMultiFrameMoovBox();
        if (!finalMoov.empty()) {
            writeToFile(finalMoov.data(), finalMoov.size());
            LOG(INFO) << "[MP4Muxer] Written final moov box: " << finalMoov.size() << " bytes";
            
            // CRITICAL: Force moov box to disk immediately
            if (m_fd != -1) {
                fsync(m_fd);
                LOG(INFO) << "[MP4Muxer] Forced moov box sync to disk";
            }
        }
    }
    
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
    
    // CRITICAL: Periodic sync every 10 frames to prevent data loss
    static int writeCounter = 0;
    writeCounter++;
    if (writeCounter % 10 == 0) {
        fsync(m_fd);
        LOG(DEBUG) << "[MP4Muxer] Periodic sync after " << writeCounter << " writes";
    }
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

// MP4 structure creation helpers (refactored to avoid duplication)
std::vector<uint8_t> MP4Muxer::createFtypBox() {
    std::vector<uint8_t> ftyp;
    
    // Manual big-endian write instead of write32 (since this is static)
    ftyp.insert(ftyp.end(), {0x00, 0x00, 0x00, 0x00}); // size placeholder
    ftyp.insert(ftyp.end(), {'f', 't', 'y', 'p'});
    ftyp.insert(ftyp.end(), {'i', 's', 'o', 'm'}); // major brand
    ftyp.insert(ftyp.end(), {0x00, 0x00, 0x02, 0x00}); // minor version (0x200)
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
    
    return ftyp;
}

// Static helper functions for MP4 creation
static void writeU32(std::vector<uint8_t>& vec, uint32_t value) {
    vec.push_back((value >> 24) & 0xFF);
    vec.push_back((value >> 16) & 0xFF);
    vec.push_back((value >> 8) & 0xFF);
    vec.push_back(value & 0xFF);
}

static void writeU16(std::vector<uint8_t>& vec, uint16_t value) {
    vec.push_back((value >> 8) & 0xFF);
    vec.push_back(value & 0xFF);
}

static void writeU8(std::vector<uint8_t>& vec, uint8_t value) {
    vec.push_back(value);
}

std::vector<uint8_t> MP4Muxer::createVideoTrackMoovBox(const std::string& sps, const std::string& pps, 
                                                        int width, int height, int fps, uint32_t frameCount) {
    std::vector<uint8_t> moov;
    writeU32(moov, 0); // size placeholder
    moov.insert(moov.end(), {'m', 'o', 'o', 'v'});

    // mvhd box (movie header)
    writeU32(moov, 108); // box size
    moov.insert(moov.end(), {'m', 'v', 'h', 'd'});
    writeU8(moov, 0); // version
    writeU8(moov, 0); writeU8(moov, 0); writeU8(moov, 0); // flags
    writeU32(moov, 0); // creation_time
    writeU32(moov, 0); // modification_time
    writeU32(moov, fps * 1000); // timescale (fps * 1000 per second)
    writeU32(moov, frameCount * 1000); // duration (frameCount * 1000 units at fps*1000 timescale)
    writeU32(moov, 0x00010000); // rate (1.0)
    writeU16(moov, 0x0100); // volume (1.0)
    writeU16(moov, 0); // reserved
    writeU32(moov, 0); writeU32(moov, 0); // reserved
    
    // transformation matrix (identity)
    writeU32(moov, 0x00010000); writeU32(moov, 0); writeU32(moov, 0);
    writeU32(moov, 0); writeU32(moov, 0x00010000); writeU32(moov, 0);
    writeU32(moov, 0); writeU32(moov, 0); writeU32(moov, 0x40000000);
    
    // pre_defined
    for (int i = 0; i < 6; i++) writeU32(moov, 0);
    writeU32(moov, 2); // next_track_ID

    // Track box (trak)
    std::vector<uint8_t> trak;
    writeU32(trak, 0); // size placeholder
    trak.insert(trak.end(), {'t', 'r', 'a', 'k'});

    // Track header (tkhd)
    writeU32(trak, 92); // box size
    trak.insert(trak.end(), {'t', 'k', 'h', 'd'});
    writeU8(trak, 0); // version
    writeU8(trak, 0); writeU8(trak, 0); writeU8(trak, 0x07); // flags (track enabled)
    writeU32(trak, 0); // creation_time
    writeU32(trak, 0); // modification_time
    writeU32(trak, 1); // track_ID
    writeU32(trak, 0); // reserved
    writeU32(trak, 1000); // duration
    writeU32(trak, 0); writeU32(trak, 0); // reserved
    writeU16(trak, 0); // layer
    writeU16(trak, 0); // alternate_group
    writeU16(trak, 0); // volume
    writeU16(trak, 0); // reserved
    
    // transformation matrix (identity)
    writeU32(trak, 0x00010000); writeU32(trak, 0); writeU32(trak, 0);
    writeU32(trak, 0); writeU32(trak, 0x00010000); writeU32(trak, 0);
    writeU32(trak, 0); writeU32(trak, 0); writeU32(trak, 0x40000000);
    
    writeU32(trak, width << 16); // width (32-bit fixed point)
    writeU32(trak, height << 16); // height (32-bit fixed point)

    // Media box (mdia)
    std::vector<uint8_t> mdia;
    writeU32(mdia, 0); // size placeholder
    mdia.insert(mdia.end(), {'m', 'd', 'i', 'a'});

    // Media header (mdhd)
    writeU32(mdia, 32); // box size
    mdia.insert(mdia.end(), {'m', 'd', 'h', 'd'});
    writeU8(mdia, 0); // version
    writeU8(mdia, 0); writeU8(mdia, 0); writeU8(mdia, 0); // flags
    writeU32(mdia, 0); // creation_time
    writeU32(mdia, 0); // modification_time
    writeU32(mdia, 90000); // timescale (typical for video)
    writeU32(mdia, 90000); // duration
    writeU16(mdia, 0x55c4); // language (und)
    writeU16(mdia, 0); // pre_defined

    // Handler reference (hdlr)
    writeU32(mdia, 45); // box size
    mdia.insert(mdia.end(), {'h', 'd', 'l', 'r'});
    writeU8(mdia, 0); // version
    writeU8(mdia, 0); writeU8(mdia, 0); writeU8(mdia, 0); // flags
    writeU32(mdia, 0); // pre_defined
    mdia.insert(mdia.end(), {'v', 'i', 'd', 'e'}); // handler_type
    writeU32(mdia, 0); writeU32(mdia, 0); writeU32(mdia, 0); // reserved
    mdia.insert(mdia.end(), {'V', 'i', 'd', 'e', 'o', 'H', 'a', 'n', 'd', 'l', 'e', 'r', '\0'}); // name

    // Media information (minf)
    std::vector<uint8_t> minf;
    writeU32(minf, 0); // size placeholder
    minf.insert(minf.end(), {'m', 'i', 'n', 'f'});

    // Video media header (vmhd)
    writeU32(minf, 20); // box size
    minf.insert(minf.end(), {'v', 'm', 'h', 'd'});
    writeU8(minf, 0); // version
    writeU8(minf, 0); writeU8(minf, 0); writeU8(minf, 1); // flags
    writeU16(minf, 0); // graphicsmode
    writeU16(minf, 0); writeU16(minf, 0); writeU16(minf, 0); // opcolor

    // Data information (dinf)
    writeU32(minf, 36); // box size
    minf.insert(minf.end(), {'d', 'i', 'n', 'f'});
    writeU32(minf, 28); // dref box size
    minf.insert(minf.end(), {'d', 'r', 'e', 'f'});
    writeU8(minf, 0); // version
    writeU8(minf, 0); writeU8(minf, 0); writeU8(minf, 0); // flags
    writeU32(minf, 1); // entry_count
    writeU32(minf, 12); // url box size
    minf.insert(minf.end(), {'u', 'r', 'l', ' '});
    writeU8(minf, 0); // version
    writeU8(minf, 0); writeU8(minf, 0); writeU8(minf, 1); // flags (self-contained)

    // Sample table (stbl)
    std::vector<uint8_t> stbl;
    writeU32(stbl, 0); // size placeholder
    stbl.insert(stbl.end(), {'s', 't', 'b', 'l'});

    // Sample description (stsd)
    std::vector<uint8_t> stsd;
    writeU32(stsd, 0); // size placeholder
    stsd.insert(stsd.end(), {'s', 't', 's', 'd'});
    writeU8(stsd, 0); // version
    writeU8(stsd, 0); writeU8(stsd, 0); writeU8(stsd, 0); // flags
    writeU32(stsd, 1); // entry_count

    // AVC1 sample entry
    std::vector<uint8_t> avc1;
    writeU32(avc1, 0); // size placeholder
    avc1.insert(avc1.end(), {'a', 'v', 'c', '1'});
    writeU16(avc1, 0); writeU16(avc1, 0); writeU16(avc1, 0); // reserved
    writeU16(avc1, 1); // data_reference_index
    writeU16(avc1, 0); writeU16(avc1, 0); // pre_defined
    writeU32(avc1, 0); writeU32(avc1, 0); writeU32(avc1, 0); // reserved
    writeU16(avc1, width); // width
    writeU16(avc1, height); // height
    writeU32(avc1, 0x00480000); // horizresolution (72 dpi)
    writeU32(avc1, 0x00480000); // vertresolution (72 dpi)
    writeU32(avc1, 0); // reserved
    writeU16(avc1, 1); // frame_count
    for (int i = 0; i < 32; i++) writeU8(avc1, 0); // compressorname
    writeU16(avc1, 0x0018); // depth
    writeU16(avc1, 0xFFFF); // pre_defined

    // AVC configuration (avcC)
    std::vector<uint8_t> avcC;
    writeU32(avcC, 0); // size placeholder
    avcC.insert(avcC.end(), {'a', 'v', 'c', 'C'});
    writeU8(avcC, 1); // configurationVersion
    writeU8(avcC, sps.size() > 1 ? sps[1] : 0x64); // AVCProfileIndication
    writeU8(avcC, sps.size() > 2 ? sps[2] : 0x00); // profile_compatibility
    writeU8(avcC, sps.size() > 3 ? sps[3] : 0x1f); // AVCLevelIndication
    writeU8(avcC, 0xFF); // lengthSizeMinusOne (4 bytes)
    writeU8(avcC, 0xE1); // numOfSequenceParameterSets (1)
    writeU16(avcC, sps.size()); // sequenceParameterSetLength
    avcC.insert(avcC.end(), sps.begin(), sps.end()); // SPS data
    writeU8(avcC, 1); // numOfPictureParameterSets
    writeU16(avcC, pps.size()); // pictureParameterSetLength
    avcC.insert(avcC.end(), pps.begin(), pps.end()); // PPS data

    // Update avcC size
    uint32_t avcCSize = avcC.size();
    avcC[0] = (avcCSize >> 24) & 0xFF;
    avcC[1] = (avcCSize >> 16) & 0xFF;
    avcC[2] = (avcCSize >> 8) & 0xFF;
    avcC[3] = avcCSize & 0xFF;

    // Add avcC to avc1
    avc1.insert(avc1.end(), avcC.begin(), avcC.end());

    // Update avc1 size
    uint32_t avc1Size = avc1.size();
    avc1[0] = (avc1Size >> 24) & 0xFF;
    avc1[1] = (avc1Size >> 16) & 0xFF;
    avc1[2] = (avc1Size >> 8) & 0xFF;
    avc1[3] = avc1Size & 0xFF;

    // Add avc1 to stsd
    stsd.insert(stsd.end(), avc1.begin(), avc1.end());

    // Update stsd size
    uint32_t stsdSize = stsd.size();
    stsd[0] = (stsdSize >> 24) & 0xFF;
    stsd[1] = (stsdSize >> 16) & 0xFF;
    stsd[2] = (stsdSize >> 8) & 0xFF;
    stsd[3] = stsdSize & 0xFF;

    // Add stsd to stbl
    stbl.insert(stbl.end(), stsd.begin(), stsd.end());

    // Update stbl size
    uint32_t stblSize = stbl.size();
    stbl[0] = (stblSize >> 24) & 0xFF;
    stbl[1] = (stblSize >> 16) & 0xFF;
    stbl[2] = (stblSize >> 8) & 0xFF;
    stbl[3] = stblSize & 0xFF;

    // Sample time table (stts)
    std::vector<uint8_t> stts;
    writeU32(stts, 24); // box size
    stts.insert(stts.end(), {'s', 't', 't', 's'});
    writeU8(stts, 0); // version
    writeU8(stts, 0); writeU8(stts, 0); writeU8(stts, 0); // flags
    writeU32(stts, 1); // entry_count
    writeU32(stts, 1); // sample_count
    writeU32(stts, 1000); // sample_delta
    
    stbl.insert(stbl.end(), stts.begin(), stts.end());

    // Sync sample table (stss)
    std::vector<uint8_t> stss;
    writeU32(stss, 20); // box size
    stss.insert(stss.end(), {'s', 't', 's', 's'});
    writeU8(stss, 0); // version
    writeU8(stss, 0); writeU8(stss, 0); writeU8(stss, 0); // flags
    writeU32(stss, 1); // entry_count
    writeU32(stss, 1); // sample_number
    
    stbl.insert(stbl.end(), stss.begin(), stss.end());

    // Sample to chunk table (stsc)
    std::vector<uint8_t> stsc;
    writeU32(stsc, 28); // box size
    stsc.insert(stsc.end(), {'s', 't', 's', 'c'});
    writeU8(stsc, 0); // version
    writeU8(stsc, 0); writeU8(stsc, 0); writeU8(stsc, 0); // flags
    writeU32(stsc, 1); // entry_count
    writeU32(stsc, 1); // first_chunk
    writeU32(stsc, 1); // samples_per_chunk
    writeU32(stsc, 1); // sample_description_index
    
    stbl.insert(stbl.end(), stsc.begin(), stsc.end());

    // Sample size table (stsz)
    std::vector<uint8_t> stsz;
    writeU32(stsz, 20); // box size
    stsz.insert(stsz.end(), {'s', 't', 's', 'z'});
    writeU8(stsz, 0); // version
    writeU8(stsz, 0); writeU8(stsz, 0); writeU8(stsz, 0); // flags
    writeU32(stsz, 0); // sample_size (0 = variable sizes)
    writeU32(stsz, 0); // sample_count
    
    stbl.insert(stbl.end(), stsz.begin(), stsz.end());

    // Chunk offset table (stco)
    std::vector<uint8_t> stco;
    writeU32(stco, 20); // box size
    stco.insert(stco.end(), {'s', 't', 'c', 'o'});
    writeU8(stco, 0); // version
    writeU8(stco, 0); writeU8(stco, 0); writeU8(stco, 0); // flags
    writeU32(stco, 1); // entry_count
    writeU32(stco, 40); // chunk_offset (placeholder)
    
    stbl.insert(stbl.end(), stco.begin(), stco.end());

    // Add stbl to minf
    minf.insert(minf.end(), stbl.begin(), stbl.end());

    // Update minf size
    uint32_t minfSize = minf.size();
    minf[0] = (minfSize >> 24) & 0xFF;
    minf[1] = (minfSize >> 16) & 0xFF;
    minf[2] = (minfSize >> 8) & 0xFF;
    minf[3] = minfSize & 0xFF;

    // Add minf to mdia
    mdia.insert(mdia.end(), minf.begin(), minf.end());

    // Update mdia size
    uint32_t mdiaSize = mdia.size();
    mdia[0] = (mdiaSize >> 24) & 0xFF;
    mdia[1] = (mdiaSize >> 16) & 0xFF;
    mdia[2] = (mdiaSize >> 8) & 0xFF;
    mdia[3] = mdiaSize & 0xFF;

    // Add mdia to trak
    trak.insert(trak.end(), mdia.begin(), mdia.end());

    // Update trak size
    uint32_t trakSize = trak.size();
    trak[0] = (trakSize >> 24) & 0xFF;
    trak[1] = (trakSize >> 16) & 0xFF;
    trak[2] = (trakSize >> 8) & 0xFF;
    trak[3] = trakSize & 0xFF;

    // Add trak to moov
    moov.insert(moov.end(), trak.begin(), trak.end());

    // Update moov size
    uint32_t moovSize = moov.size();
    moov[0] = (moovSize >> 24) & 0xFF;
    moov[1] = (moovSize >> 16) & 0xFF;
    moov[2] = (moovSize >> 8) & 0xFF;
    moov[3] = moovSize & 0xFF;
    
    return moov;
    
}

std::vector<uint8_t> MP4Muxer::createMdatBox(const std::string& sps, const std::string& pps, 
                                              const unsigned char* h264Data, size_t dataSize) {
    std::vector<uint8_t> mdat;
    
    // mdat header placeholder
    writeU32(mdat, 0); // size placeholder
    mdat.insert(mdat.end(), {'m', 'd', 'a', 't'});
    
    // Add SPS with length prefix (as in working version)
    if (!sps.empty()) {
        writeU32(mdat, sps.size());
        mdat.insert(mdat.end(), sps.begin(), sps.end());
    }
    
    // Add PPS with length prefix (as in working version)
    if (!pps.empty()) {
        writeU32(mdat, pps.size());
        mdat.insert(mdat.end(), pps.begin(), pps.end());
    }
    
    // Add H264 frame with length prefix (as in working version)
    if (h264Data && dataSize > 0) {
        writeU32(mdat, dataSize);
        mdat.insert(mdat.end(), h264Data, h264Data + dataSize);
    }
    
    // Update mdat size
    uint32_t mdatSize = mdat.size();
    mdat[0] = (mdatSize >> 24) & 0xFF;
    mdat[1] = (mdatSize >> 16) & 0xFF;
    mdat[2] = (mdatSize >> 8) & 0xFF;
    mdat[3] = mdatSize & 0xFF;
    
    return mdat;
}

// Write MP4 header structure ONCE (refactored using helper methods)
bool MP4Muxer::writeMP4Header() {
    // Create minimal but valid MP4 structure
    std::vector<uint8_t> mp4Header;

    // 1. ftyp box
    std::vector<uint8_t> ftyp = createFtypBox();
    mp4Header.insert(mp4Header.end(), ftyp.begin(), ftyp.end());

    // 2. Minimal moov box (for compatibility)
    std::vector<uint8_t> moov = createMinimalMoovBox();
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

// Static method for creating MP4 snapshot in memory (FIXED with proper video track)
std::vector<uint8_t> MP4Muxer::createMP4Snapshot(const unsigned char* h264Data, size_t dataSize,
                                                  const std::string& sps, const std::string& pps,
                                                  int width, int height) {
    std::vector<uint8_t> mp4Data;
    
    if (!h264Data || dataSize == 0 || sps.empty() || pps.empty() || width <= 0 || height <= 0) {
        LOG(ERROR) << "[MP4Muxer] Invalid parameters for MP4 snapshot creation";
        return mp4Data;
    }
    
    LOG(DEBUG) << "[MP4Muxer] Creating MP4 snapshot: " << dataSize << " bytes, " << width << "x" << height;
    
    // 1. ftyp box (exactly as in REFERENCE.cpp)
    std::vector<uint8_t> ftyp;
    writeU32(ftyp, 0); // size placeholder
    ftyp.insert(ftyp.end(), {'f', 't', 'y', 'p'});
    ftyp.insert(ftyp.end(), {'i', 's', 'o', 'm'}); // major brand
    writeU32(ftyp, 0x200); // minor version
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
    
    mp4Data.insert(mp4Data.end(), ftyp.begin(), ftyp.end());

    // 2. mdat box (exactly as in REFERENCE.cpp)
    std::vector<uint8_t> mdat;
    writeU32(mdat, 0); // size placeholder
    mdat.insert(mdat.end(), {'m', 'd', 'a', 't'});
    
    // SPS
    if (!sps.empty()) {
        writeU32(mdat, sps.size());
        mdat.insert(mdat.end(), sps.begin(), sps.end());
    }
    
    // PPS  
    if (!pps.empty()) {
        writeU32(mdat, pps.size());
        mdat.insert(mdat.end(), pps.begin(), pps.end());
    }
    
    // H264 frame
    writeU32(mdat, dataSize);
    mdat.insert(mdat.end(), h264Data, h264Data + dataSize);
    
    // Update mdat size
    uint32_t mdatSize = mdat.size();
    mdat[0] = (mdatSize >> 24) & 0xFF;
    mdat[1] = (mdatSize >> 16) & 0xFF;
    mdat[2] = (mdatSize >> 8) & 0xFF;
    mdat[3] = mdatSize & 0xFF;
    
    mp4Data.insert(mp4Data.end(), mdat.begin(), mdat.end());

    // Calculate the correct chunk offset (start of SPS in mdat)
    uint32_t mdatDataOffset = ftypSize + 8; // ftyp + mdat header (8 bytes)

    // 3. moov box (FULL VERSION as in REFERENCE.cpp)
    std::vector<uint8_t> moov;
    writeU32(moov, 0); // size placeholder
    moov.insert(moov.end(), {'m', 'o', 'o', 'v'});

    // 3.1 mvhd box
    std::vector<uint8_t> mvhd;
    writeU32(mvhd, 108); // box size
    mvhd.insert(mvhd.end(), {'m', 'v', 'h', 'd'});
    writeU8(mvhd, 0); // version
    writeU8(mvhd, 0); writeU8(mvhd, 0); writeU8(mvhd, 0); // flags
    writeU32(mvhd, 0); // creation_time
    writeU32(mvhd, 0); // modification_time
    writeU32(mvhd, m_fps * 1000); // timescale (fps * 1000 per second)
    writeU32(mvhd, m_frameCount * 1000); // duration (frameCount * 1000 units at fps*1000 timescale)
    writeU32(mvhd, 0x00010000); // rate (1.0)
    writeU16(mvhd, 0x0100); // volume (1.0)
    writeU16(mvhd, 0); // reserved
    writeU32(mvhd, 0); writeU32(mvhd, 0); // reserved
    // transformation matrix (identity)
    writeU32(mvhd, 0x00010000); writeU32(mvhd, 0); writeU32(mvhd, 0);
    writeU32(mvhd, 0); writeU32(mvhd, 0x00010000); writeU32(mvhd, 0);
    writeU32(mvhd, 0); writeU32(mvhd, 0); writeU32(mvhd, 0x40000000);
    // pre_defined
    for (int i = 0; i < 6; i++) writeU32(mvhd, 0);
    writeU32(mvhd, 2); // next_track_ID
    
    moov.insert(moov.end(), mvhd.begin(), mvhd.end());

    // 3.2 trak box
    std::vector<uint8_t> trak;
    writeU32(trak, 0); // size placeholder
    trak.insert(trak.end(), {'t', 'r', 'a', 'k'});

    // 3.2.1 tkhd box
    std::vector<uint8_t> tkhd;
    writeU32(tkhd, 92); // box size
    tkhd.insert(tkhd.end(), {'t', 'k', 'h', 'd'});
    writeU8(tkhd, 0); // version
    writeU8(tkhd, 0); writeU8(tkhd, 0); writeU8(tkhd, 0xF); // flags (track enabled)
    writeU32(tkhd, 0); // creation_time
    writeU32(tkhd, 0); // modification_time
    writeU32(tkhd, 1); // track_ID
    writeU32(tkhd, 0); // reserved
    writeU32(tkhd, 1000); // duration
    writeU32(tkhd, 0); writeU32(tkhd, 0); // reserved
    writeU16(tkhd, 0); // layer
    writeU16(tkhd, 0); // alternate_group
    writeU16(tkhd, 0); // volume
    writeU16(tkhd, 0); // reserved
    // transformation matrix (identity)
    writeU32(tkhd, 0x00010000); writeU32(tkhd, 0); writeU32(tkhd, 0);
    writeU32(tkhd, 0); writeU32(tkhd, 0x00010000); writeU32(tkhd, 0);
    writeU32(tkhd, 0); writeU32(tkhd, 0); writeU32(tkhd, 0x40000000);
    writeU32(tkhd, width << 16); // width
    writeU32(tkhd, height << 16); // height
    
    trak.insert(trak.end(), tkhd.begin(), tkhd.end());

    // 3.2.2 mdia box
    std::vector<uint8_t> mdia;
    writeU32(mdia, 0); // size placeholder
    mdia.insert(mdia.end(), {'m', 'd', 'i', 'a'});

    // 3.2.2.1 mdhd box - FIXED: consistent timescale
    std::vector<uint8_t> mdhd;
    writeU32(mdhd, 32); // box size
    mdhd.insert(mdhd.end(), {'m', 'd', 'h', 'd'});
    writeU8(mdhd, 0); // version
    writeU8(mdhd, 0); writeU8(mdhd, 0); writeU8(mdhd, 0); // flags
    writeU32(mdhd, 0); // creation_time
    writeU32(mdhd, 0); // modification_time
    writeU32(mdhd, 25000); // timescale (25fps)
    writeU32(mdhd, m_frameCount * 1000); // duration (1000 units per frame at 25000 timescale = 25fps)
    writeU16(mdhd, 0x55c4); // language (und)
    writeU16(mdhd, 0); // pre_defined
    
    mdia.insert(mdia.end(), mdhd.begin(), mdhd.end());

    // 3.2.2.2 hdlr box
    std::vector<uint8_t> hdlr;
    writeU32(hdlr, 33); // box size
    hdlr.insert(hdlr.end(), {'h', 'd', 'l', 'r'});
    writeU8(hdlr, 0); // version
    writeU8(hdlr, 0); writeU8(hdlr, 0); writeU8(hdlr, 0); // flags
    writeU32(hdlr, 0); // pre_defined
    hdlr.insert(hdlr.end(), {'v', 'i', 'd', 'e'}); // handler_type
    writeU32(hdlr, 0); writeU32(hdlr, 0); writeU32(hdlr, 0); // reserved
    hdlr.push_back(0); // name (empty string)
    
    mdia.insert(mdia.end(), hdlr.begin(), hdlr.end());

    // 3.2.2.3 minf box
    std::vector<uint8_t> minf;
    writeU32(minf, 0); // size placeholder
    minf.insert(minf.end(), {'m', 'i', 'n', 'f'});

    // 3.2.2.3.1 vmhd box
    std::vector<uint8_t> vmhd;
    writeU32(vmhd, 20); // box size
    vmhd.insert(vmhd.end(), {'v', 'm', 'h', 'd'});
    writeU8(vmhd, 0); // version
    writeU8(vmhd, 0); writeU8(vmhd, 0); writeU8(vmhd, 1); // flags
    writeU16(vmhd, 0); // graphicsmode
    writeU16(vmhd, 0); writeU16(vmhd, 0); writeU16(vmhd, 0); // opcolor
    
    minf.insert(minf.end(), vmhd.begin(), vmhd.end());

    // 3.2.2.3.2 dinf box - FIXED: correct dref
    std::vector<uint8_t> dinf;
    writeU32(dinf, 36); // box size
    dinf.insert(dinf.end(), {'d', 'i', 'n', 'f'});
    
    // dref box
    std::vector<uint8_t> dref;
    writeU32(dref, 28); // box size
    dref.insert(dref.end(), {'d', 'r', 'e', 'f'});
    writeU8(dref, 0); // version
    writeU8(dref, 0); writeU8(dref, 0); writeU8(dref, 0); // flags
    writeU32(dref, 1); // entry_count
    
    // url box - FIXED: correct type
    writeU32(dref, 12); // box size
    dref.insert(dref.end(), {'u', 'r', 'l', ' '}); // CORRECT type
    writeU8(dref, 0); // version
    writeU8(dref, 0); writeU8(dref, 0); writeU8(dref, 1); // flags (self-contained)
    
    dinf.insert(dinf.end(), dref.begin(), dref.end());
    minf.insert(minf.end(), dinf.begin(), dinf.end());

    // 3.2.2.3.3 stbl box
    std::vector<uint8_t> stbl;
    writeU32(stbl, 0); // size placeholder
    stbl.insert(stbl.end(), {'s', 't', 'b', 'l'});

    // avcC configuration record
    std::vector<uint8_t> avcC;
    writeU8(avcC, 1); // configurationVersion
    writeU8(avcC, sps.size() >= 4 ? sps[1] : 0x64); // AVCProfileIndication
    writeU8(avcC, sps.size() >= 4 ? sps[2] : 0x00); // profile_compatibility
    writeU8(avcC, sps.size() >= 4 ? sps[3] : 0x28); // AVCLevelIndication
    writeU8(avcC, 0xFF); // lengthSizeMinusOne (4 bytes)
    writeU8(avcC, 0xE1); // numOfSequenceParameterSets
    writeU16(avcC, sps.size());
    avcC.insert(avcC.end(), sps.begin(), sps.end());
    writeU8(avcC, 1); // numOfPictureParameterSets
    writeU16(avcC, pps.size());
    avcC.insert(avcC.end(), pps.begin(), pps.end());

    // stsd box
    std::vector<uint8_t> stsd;
    writeU32(stsd, 0); // size placeholder
    stsd.insert(stsd.end(), {'s', 't', 's', 'd'});
    writeU8(stsd, 0); // version
    writeU8(stsd, 0); writeU8(stsd, 0); writeU8(stsd, 0); // flags
    writeU32(stsd, 1); // entry_count

    // avc1 sample entry
    std::vector<uint8_t> avc1;
    writeU32(avc1, 0); // size placeholder
    avc1.insert(avc1.end(), {'a', 'v', 'c', '1'});
    // reserved
    for (int i = 0; i < 6; i++) writeU8(avc1, 0);
    writeU16(avc1, 1); // data_reference_index
    // pre_defined and reserved
    for (int i = 0; i < 16; i++) writeU8(avc1, 0);
    writeU16(avc1, width); // width
    writeU16(avc1, height); // height
    writeU32(avc1, 0x00480000); // horizresolution (72 dpi)
    writeU32(avc1, 0x00480000); // vertresolution (72 dpi)
    writeU32(avc1, 0); // reserved
    writeU16(avc1, 1); // frame_count
    // compressorname (32 bytes)
    for (int i = 0; i < 32; i++) writeU8(avc1, 0);
    writeU16(avc1, 0x0018); // depth
    writeU16(avc1, 0xFFFF); // pre_defined

    // avcC box
    std::vector<uint8_t> avcCBox;
    writeU32(avcCBox, avcC.size() + 8); // box size
    avcCBox.insert(avcCBox.end(), {'a', 'v', 'c', 'C'});
    avcCBox.insert(avcCBox.end(), avcC.begin(), avcC.end());
    
    avc1.insert(avc1.end(), avcCBox.begin(), avcCBox.end());
    
    // Update avc1 size
    uint32_t avc1Size = avc1.size();
    avc1[0] = (avc1Size >> 24) & 0xFF;
    avc1[1] = (avc1Size >> 16) & 0xFF;
    avc1[2] = (avc1Size >> 8) & 0xFF;
    avc1[3] = avc1Size & 0xFF;
    
    stsd.insert(stsd.end(), avc1.begin(), avc1.end());
    
    // Update stsd size
    uint32_t stsdSize = stsd.size();
    stsd[0] = (stsdSize >> 24) & 0xFF;
    stsd[1] = (stsdSize >> 16) & 0xFF;
    stsd[2] = (stsdSize >> 8) & 0xFF;
    stsd[3] = stsdSize & 0xFF;
    
    stbl.insert(stbl.end(), stsd.begin(), stsd.end());

    // stts box (time-to-sample)
    std::vector<uint8_t> stts;
    writeU32(stts, 24); // box size
    stts.insert(stts.end(), {'s', 't', 't', 's'});
    writeU8(stts, 0); // version
    writeU8(stts, 0); writeU8(stts, 0); writeU8(stts, 0); // flags
    writeU32(stts, 1); // entry_count
    writeU32(stts, 1); // sample_count
    writeU32(stts, 1000); // sample_delta
    
    stbl.insert(stbl.end(), stts.begin(), stts.end());

    // stss box (sync sample - keyframes)
    std::vector<uint8_t> stss;
    writeU32(stss, 20); // box size
    stss.insert(stss.end(), {'s', 't', 's', 's'});
    writeU8(stss, 0); // version
    writeU8(stss, 0); writeU8(stss, 0); writeU8(stss, 0); // flags
    writeU32(stss, 1); // entry_count
    writeU32(stss, 1); // sample_number
    
    stbl.insert(stbl.end(), stss.begin(), stss.end());

    // stsc box (sample-to-chunk)
    std::vector<uint8_t> stsc;
    writeU32(stsc, 28); // box size
    stsc.insert(stsc.end(), {'s', 't', 's', 'c'});
    writeU8(stsc, 0); // version
    writeU8(stsc, 0); writeU8(stsc, 0); writeU8(stsc, 0); // flags
    writeU32(stsc, 1); // entry_count
    writeU32(stsc, 1); // first_chunk
    writeU32(stsc, 1); // samples_per_chunk
    writeU32(stsc, 1); // sample_description_index
    
    stbl.insert(stbl.end(), stsc.begin(), stsc.end());

    // stsz box (sample sizes) - FIXED: correct sample count and size
    std::vector<uint8_t> stsz;
    writeU32(stsz, 24); // box size (FIXED: 20 + 4 bytes for one sample)
    stsz.insert(stsz.end(), {'s', 't', 's', 'z'});
    writeU8(stsz, 0); // version
    writeU8(stsz, 0); writeU8(stsz, 0); writeU8(stsz, 0); // flags
    writeU32(stsz, 0); // sample_size (0 = variable sizes)
    writeU32(stsz, 1); // sample_count (FIXED: was 0)
    
    // Calculate total sample size (SPS + PPS + H264 with length prefixes)
    uint32_t totalSampleSize = 4 + sps.size() + 4 + pps.size() + 4 + dataSize;
    writeU32(stsz, totalSampleSize);
    
    stbl.insert(stbl.end(), stsz.begin(), stsz.end());

    // stco box (chunk offsets) - FIXED: correct offset calculation
    std::vector<uint8_t> stco;
    writeU32(stco, 20); // box size
    stco.insert(stco.end(), {'s', 't', 'c', 'o'});
    writeU8(stco, 0); // version
    writeU8(stco, 0); writeU8(stco, 0); writeU8(stco, 0); // flags
    writeU32(stco, 1); // entry_count
    writeU32(stco, mdatDataOffset); // chunk_offset (FIXED: real offset to data)
    
    stbl.insert(stbl.end(), stco.begin(), stco.end());

    // Update stbl size
    uint32_t stblSize = stbl.size();
    stbl[0] = (stblSize >> 24) & 0xFF;
    stbl[1] = (stblSize >> 16) & 0xFF;
    stbl[2] = (stblSize >> 8) & 0xFF;
    stbl[3] = stblSize & 0xFF;

    // Add stbl to minf
    minf.insert(minf.end(), stbl.begin(), stbl.end());

    // Update minf size
    uint32_t minfSize = minf.size();
    minf[0] = (minfSize >> 24) & 0xFF;
    minf[1] = (minfSize >> 16) & 0xFF;
    minf[2] = (minfSize >> 8) & 0xFF;
    minf[3] = minfSize & 0xFF;

    // Add minf to mdia
    mdia.insert(mdia.end(), minf.begin(), minf.end());

    // Update mdia size
    uint32_t mdiaSize = mdia.size();
    mdia[0] = (mdiaSize >> 24) & 0xFF;
    mdia[1] = (mdiaSize >> 16) & 0xFF;
    mdia[2] = (mdiaSize >> 8) & 0xFF;
    mdia[3] = mdiaSize & 0xFF;

    // Add mdia to trak
    trak.insert(trak.end(), mdia.begin(), mdia.end());

    // Update trak size
    uint32_t trakSize = trak.size();
    trak[0] = (trakSize >> 24) & 0xFF;
    trak[1] = (trakSize >> 16) & 0xFF;
    trak[2] = (trakSize >> 8) & 0xFF;
    trak[3] = trakSize & 0xFF;

    // Add trak to moov
    moov.insert(moov.end(), trak.begin(), trak.end());

    // Update moov size - FIXED: remove duplicate lines
    uint32_t moovSize = moov.size();
    moov[0] = (moovSize >> 24) & 0xFF;
    moov[1] = (moovSize >> 16) & 0xFF;
    moov[2] = (moovSize >> 8) & 0xFF;
    moov[3] = moovSize & 0xFF;
    
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

std::vector<uint8_t> MP4Muxer::createMinimalMoovBox() {
    std::vector<uint8_t> moov;
    writeU32(moov, 108); // box size
    moov.insert(moov.end(), {'m', 'v', 'h', 'd'});
    writeU8(moov, 0); // version
    writeU8(moov, 0); writeU8(moov, 0); writeU8(moov, 0); // flags
    writeU32(moov, 0); // creation_time
    writeU32(moov, 0); // modification_time
    writeU32(moov, 1000); // timescale
    writeU32(moov, 1000); // duration
    writeU32(moov, 0x00010000); // rate (1.0)
    writeU16(moov, 0x0100); // volume (1.0)
    writeU16(moov, 0); // reserved
    writeU32(moov, 0); writeU32(moov, 0); // reserved
    
    // transformation matrix (identity)
    writeU32(moov, 0x00010000); writeU32(moov, 0); writeU32(moov, 0);
    writeU32(moov, 0); writeU32(moov, 0x00010000); writeU32(moov, 0);
    writeU32(moov, 0); writeU32(moov, 0); writeU32(moov, 0x40000000);
    
    // pre_defined
    for (int i = 0; i < 6; i++) writeU32(moov, 0);
    writeU32(moov, 2); // next_track_ID
    
    // Update moov size
    uint32_t moovSize = moov.size();
    moov[0] = (moovSize >> 24) & 0xFF;
    moov[1] = (moovSize >> 16) & 0xFF;
    moov[2] = (moovSize >> 8) & 0xFF;
    moov[3] = moovSize & 0xFF;
    
    return moov;
}

std::vector<uint8_t> MP4Muxer::createMultiFrameMoovBox() {
    if (m_frames.empty()) {
        return std::vector<uint8_t>();
    }
    
    std::vector<uint8_t> moov;
    writeU32(moov, 0); // size placeholder
    moov.insert(moov.end(), {'m', 'o', 'o', 'v'});

    // mvhd box
    std::vector<uint8_t> mvhd;
    writeU32(mvhd, 108); // box size
    mvhd.insert(mvhd.end(), {'m', 'v', 'h', 'd'});
    writeU8(mvhd, 0); // version
    writeU8(mvhd, 0); writeU8(mvhd, 0); writeU8(mvhd, 0); // flags
    writeU32(mvhd, 0); // creation_time
    writeU32(mvhd, 0); // modification_time
    writeU32(mvhd, 1000); // timescale
    writeU32(mvhd, m_frameCount * 1000); // duration (frameCount seconds at 1fps)
    writeU32(mvhd, 0x00010000); // rate (1.0)
    writeU16(mvhd, 0x0100); // volume (1.0)
    writeU16(mvhd, 0); // reserved
    writeU32(mvhd, 0); writeU32(mvhd, 0); // reserved
    // transformation matrix (identity)
    writeU32(mvhd, 0x00010000); writeU32(mvhd, 0); writeU32(mvhd, 0);
    writeU32(mvhd, 0); writeU32(mvhd, 0x00010000); writeU32(mvhd, 0);
    writeU32(mvhd, 0); writeU32(mvhd, 0); writeU32(mvhd, 0x40000000);
    // pre_defined
    for (int i = 0; i < 6; i++) writeU32(mvhd, 0);
    writeU32(mvhd, 2); // next_track_ID
    
    moov.insert(moov.end(), mvhd.begin(), mvhd.end());

    // trak box
    std::vector<uint8_t> trak;
    writeU32(trak, 0); // size placeholder
    trak.insert(trak.end(), {'t', 'r', 'a', 'k'});

    // tkhd box
    std::vector<uint8_t> tkhd;
    writeU32(tkhd, 92); // box size
    tkhd.insert(tkhd.end(), {'t', 'k', 'h', 'd'});
    writeU8(tkhd, 0); // version
    writeU8(tkhd, 0); writeU8(tkhd, 0); writeU8(tkhd, 0xF); // flags (track enabled)
    writeU32(tkhd, 0); // creation_time
    writeU32(tkhd, 0); // modification_time
    writeU32(tkhd, 1); // track_ID
    writeU32(tkhd, 0); // reserved
    writeU32(tkhd, m_frameCount * 1000); // duration
    writeU32(tkhd, 0); writeU32(tkhd, 0); // reserved
    writeU16(tkhd, 0); // layer
    writeU16(tkhd, 0); // alternate_group
    writeU16(tkhd, 0); // volume
    writeU16(tkhd, 0); // reserved
    // transformation matrix (identity)
    writeU32(tkhd, 0x00010000); writeU32(tkhd, 0); writeU32(tkhd, 0);
    writeU32(tkhd, 0); writeU32(tkhd, 0x00010000); writeU32(tkhd, 0);
    writeU32(tkhd, 0); writeU32(tkhd, 0); writeU32(tkhd, 0x40000000);
    writeU32(tkhd, m_width << 16); // width
    writeU32(tkhd, m_height << 16); // height
    
    trak.insert(trak.end(), tkhd.begin(), tkhd.end());

    // mdia box
    std::vector<uint8_t> mdia;
    writeU32(mdia, 0); // size placeholder
    mdia.insert(mdia.end(), {'m', 'd', 'i', 'a'});

    // mdhd box
    std::vector<uint8_t> mdhd;
    writeU32(mdhd, 32); // box size
    mdhd.insert(mdhd.end(), {'m', 'd', 'h', 'd'});
    writeU8(mdhd, 0); // version
    writeU8(mdhd, 0); writeU8(mdhd, 0); writeU8(mdhd, 0); // flags
    writeU32(mdhd, 0); // creation_time
    writeU32(mdhd, 0); // modification_time
    writeU32(mdhd, m_fps * 1000); // timescale (dynamic fps)
    writeU32(mdhd, m_frameCount * 1000); // duration (1000 units per frame at fps*1000 timescale)
    writeU16(mdhd, 0x55c4); // language (und)
    writeU16(mdhd, 0); // pre_defined
    
    mdia.insert(mdia.end(), mdhd.begin(), mdhd.end());

    // hdlr box
    std::vector<uint8_t> hdlr;
    writeU32(hdlr, 33); // box size
    hdlr.insert(hdlr.end(), {'h', 'd', 'l', 'r'});
    writeU8(hdlr, 0); // version
    writeU8(hdlr, 0); writeU8(hdlr, 0); writeU8(hdlr, 0); // flags
    writeU32(hdlr, 0); // pre_defined
    hdlr.insert(hdlr.end(), {'v', 'i', 'd', 'e'}); // handler_type
    writeU32(hdlr, 0); writeU32(hdlr, 0); writeU32(hdlr, 0); // reserved
    hdlr.push_back(0); // name (empty string)
    
    mdia.insert(mdia.end(), hdlr.begin(), hdlr.end());

    // minf box
    std::vector<uint8_t> minf;
    writeU32(minf, 0); // size placeholder
    minf.insert(minf.end(), {'m', 'i', 'n', 'f'});

    // vmhd box
    std::vector<uint8_t> vmhd;
    writeU32(vmhd, 20); // box size
    vmhd.insert(vmhd.end(), {'v', 'm', 'h', 'd'});
    writeU8(vmhd, 0); // version
    writeU8(vmhd, 0); writeU8(vmhd, 0); writeU8(vmhd, 1); // flags
    writeU16(vmhd, 0); // graphicsmode
    writeU16(vmhd, 0); writeU16(vmhd, 0); writeU16(vmhd, 0); // opcolor
    
    minf.insert(minf.end(), vmhd.begin(), vmhd.end());

    // dinf box - FIXED: correct dref
    std::vector<uint8_t> dinf;
    writeU32(dinf, 36); // box size
    dinf.insert(dinf.end(), {'d', 'i', 'n', 'f'});
    
    // dref box
    std::vector<uint8_t> dref;
    writeU32(dref, 28); // box size
    dref.insert(dref.end(), {'d', 'r', 'e', 'f'});
    writeU8(dref, 0); // version
    writeU8(dref, 0); writeU8(dref, 0); writeU8(dref, 0); // flags
    writeU32(dref, 1); // entry_count
    
    // url box - FIXED: correct type
    writeU32(dref, 12); // box size
    dref.insert(dref.end(), {'u', 'r', 'l', ' '}); // CORRECT type
    writeU8(dref, 0); // version
    writeU8(dref, 0); writeU8(dref, 0); writeU8(dref, 1); // flags (self-contained)
    
    dinf.insert(dinf.end(), dref.begin(), dref.end());
    minf.insert(minf.end(), dinf.begin(), dinf.end());

    // stbl box for multiple frames - FIXED VERSION
    std::vector<uint8_t> stbl;
    writeU32(stbl, 0); // size placeholder
    stbl.insert(stbl.end(), {'s', 't', 'b', 'l'});

    // avcC configuration record
    std::vector<uint8_t> avcC;
    writeU8(avcC, 1); // configurationVersion
    writeU8(avcC, m_sps.size() >= 4 ? m_sps[1] : 0x64); // AVCProfileIndication
    writeU8(avcC, m_sps.size() >= 4 ? m_sps[2] : 0x00); // profile_compatibility
    writeU8(avcC, m_sps.size() >= 4 ? m_sps[3] : 0x28); // AVCLevelIndication
    writeU8(avcC, 0xFF); // lengthSizeMinusOne (4 bytes)
    writeU8(avcC, 0xE1); // numOfSequenceParameterSets
    writeU16(avcC, m_sps.size());
    avcC.insert(avcC.end(), m_sps.begin(), m_sps.end());
    writeU8(avcC, 1); // numOfPictureParameterSets
    writeU16(avcC, m_pps.size());
    avcC.insert(avcC.end(), m_pps.begin(), m_pps.end());

    // stsd box
    std::vector<uint8_t> stsd;
    writeU32(stsd, 0); // size placeholder
    stsd.insert(stsd.end(), {'s', 't', 's', 'd'});
    writeU8(stsd, 0); // version
    writeU8(stsd, 0); writeU8(stsd, 0); writeU8(stsd, 0); // flags
    writeU32(stsd, 1); // entry_count

    // avc1 sample entry
    std::vector<uint8_t> avc1;
    writeU32(avc1, 0); // size placeholder
    avc1.insert(avc1.end(), {'a', 'v', 'c', '1'});
    // reserved
    for (int i = 0; i < 6; i++) writeU8(avc1, 0);
    writeU16(avc1, 1); // data_reference_index
    // pre_defined and reserved
    for (int i = 0; i < 16; i++) writeU8(avc1, 0);
    writeU16(avc1, m_width); // width
    writeU16(avc1, m_height); // height
    writeU32(avc1, 0x00480000); // horizresolution (72 dpi)
    writeU32(avc1, 0x00480000); // vertresolution (72 dpi)
    writeU32(avc1, 0); // reserved
    writeU16(avc1, 1); // frame_count
    // compressorname (32 bytes)
    for (int i = 0; i < 32; i++) writeU8(avc1, 0);
    writeU16(avc1, 0x0018); // depth
    writeU16(avc1, 0xFFFF); // pre_defined

    // avcC box
    std::vector<uint8_t> avcCBox;
    writeU32(avcCBox, avcC.size() + 8); // box size
    avcCBox.insert(avcCBox.end(), {'a', 'v', 'c', 'C'});
    avcCBox.insert(avcCBox.end(), avcC.begin(), avcC.end());
    
    avc1.insert(avc1.end(), avcCBox.begin(), avcCBox.end());
    
    // Update avc1 size
    uint32_t avc1Size = avc1.size();
    avc1[0] = (avc1Size >> 24) & 0xFF;
    avc1[1] = (avc1Size >> 16) & 0xFF;
    avc1[2] = (avc1Size >> 8) & 0xFF;
    avc1[3] = avc1Size & 0xFF;
    
    stsd.insert(stsd.end(), avc1.begin(), avc1.end());
    
    // Update stsd size
    uint32_t stsdSize = stsd.size();
    stsd[0] = (stsdSize >> 24) & 0xFF;
    stsd[1] = (stsdSize >> 16) & 0xFF;
    stsd[2] = (stsdSize >> 8) & 0xFF;
    stsd[3] = stsdSize & 0xFF;
    
    stbl.insert(stbl.end(), stsd.begin(), stsd.end());

    // stts box (time-to-sample) - FIXED for multiple frames
    std::vector<uint8_t> stts;
    writeU32(stts, 24); // box size
    stts.insert(stts.end(), {'s', 't', 't', 's'});
    writeU8(stts, 0); // version
    writeU8(stts, 0); writeU8(stts, 0); writeU8(stts, 0); // flags
    writeU32(stts, 1); // entry_count (one entry for all frames)
    writeU32(stts, m_frameCount); // sample_count
    writeU32(stts, 1000); // sample_delta (1000 units at fps*1000 timescale = dynamic fps)
    
    stbl.insert(stbl.end(), stts.begin(), stts.end());

    // stss box (sync sample) - all frames keyframes
    std::vector<uint8_t> stss;
    writeU32(stss, 16 + m_frameCount * 4); // box size
    stss.insert(stss.end(), {'s', 't', 's', 's'});
    writeU8(stss, 0); // version
    writeU8(stss, 0); writeU8(stss, 0); writeU8(stss, 0); // flags
    writeU32(stss, m_frameCount); // entry_count
    for (uint32_t i = 0; i < m_frameCount; i++) {
        writeU32(stss, i + 1); // sample_number (1-based)
    }
    
    stbl.insert(stbl.end(), stss.begin(), stss.end());

    // stsc box (sample-to-chunk) - FIXED: each sample in separate chunk
    std::vector<uint8_t> stsc;
    writeU32(stsc, 16 + m_frameCount * 12); // box size
    stsc.insert(stsc.end(), {'s', 't', 's', 'c'});
    writeU8(stsc, 0); // version
    writeU8(stsc, 0); writeU8(stsc, 0); writeU8(stsc, 0); // flags
    writeU32(stsc, m_frameCount); // entry_count
    for (uint32_t i = 0; i < m_frameCount; i++) {
        writeU32(stsc, i + 1); // first_chunk
        writeU32(stsc, 1); // samples_per_chunk
        writeU32(stsc, 1); // sample_description_index
    }
    
    stbl.insert(stbl.end(), stsc.begin(), stsc.end());

    // stsz box (sample sizes) - FIXED for multiple frames
    std::vector<uint8_t> stsz;
    writeU32(stsz, 20 + m_frameCount * 4); // box size
    stsz.insert(stsz.end(), {'s', 't', 's', 'z'});
    writeU8(stsz, 0); // version
    writeU8(stsz, 0); writeU8(stsz, 0); writeU8(stsz, 0); // flags
    writeU32(stsz, 0); // sample_size (0 = variable sizes)
    writeU32(stsz, m_frameCount); // sample_count
    
    // Add sizes of each frame
    for (const auto& frame : m_frames) {
        writeU32(stsz, frame.size);
    }
    
    stbl.insert(stbl.end(), stsz.begin(), stsz.end());

    // stco box (chunk offsets) - FIXED: offset for each chunk
    std::vector<uint8_t> stco;
    writeU32(stco, 16 + m_frameCount * 4); // box size
    stco.insert(stco.end(), {'s', 't', 'c', 'o'});
    writeU8(stco, 0); // version
    writeU8(stco, 0); writeU8(stco, 0); writeU8(stco, 0); // flags
    writeU32(stco, m_frameCount); // entry_count
    
    // Calculate offset for each frame
    for (const auto& frame : m_frames) {
        writeU32(stco, frame.offset);
    }
    
    stbl.insert(stbl.end(), stco.begin(), stco.end());

    // Update stbl size
    uint32_t stblSize = stbl.size();
    stbl[0] = (stblSize >> 24) & 0xFF;
    stbl[1] = (stblSize >> 16) & 0xFF;
    stbl[2] = (stblSize >> 8) & 0xFF;
    stbl[3] = stblSize & 0xFF;
    
    minf.insert(minf.end(), stbl.begin(), stbl.end());

    // Update minf size
    uint32_t minfSize = minf.size();
    minf[0] = (minfSize >> 24) & 0xFF;
    minf[1] = (minfSize >> 16) & 0xFF;
    minf[2] = (minfSize >> 8) & 0xFF;
    minf[3] = minfSize & 0xFF;
    
    mdia.insert(mdia.end(), minf.begin(), minf.end());

    // Update mdia size
    uint32_t mdiaSize = mdia.size();
    mdia[0] = (mdiaSize >> 24) & 0xFF;
    mdia[1] = (mdiaSize >> 16) & 0xFF;
    mdia[2] = (mdiaSize >> 8) & 0xFF;
    mdia[3] = mdiaSize & 0xFF;
    
    trak.insert(trak.end(), mdia.begin(), mdia.end());

    // Update trak size
    uint32_t trakSize = trak.size();
    trak[0] = (trakSize >> 24) & 0xFF;
    trak[1] = (trakSize >> 16) & 0xFF;
    trak[2] = (trakSize >> 8) & 0xFF;
    trak[3] = trakSize & 0xFF;
    
    moov.insert(moov.end(), trak.begin(), trak.end());

    // Update moov size
    uint32_t moovSize = moov.size();
    moov[0] = (moovSize >> 24) & 0xFF;
    moov[1] = (moovSize >> 16) & 0xFF;
    moov[2] = (moovSize >> 8) & 0xFF;
    moov[3] = moovSize & 0xFF;
    
    return moov;
} 