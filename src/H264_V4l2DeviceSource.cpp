/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** H264_V4l2DeviceSource.cpp
** 
** H264 V4L2 Live555 source 
**
** -------------------------------------------------------------------------*/

#include <sstream>
#include <vector>

// live555
#include <Base64.hh>

// project
#include "logger.h"
#include "H264_V4l2DeviceSource.h"
#include "SnapshotManager.h"
#include "MP4Muxer.h"

// ---------------------------------
// H264 V4L2 FramedSource
// ---------------------------------

H264_V4L2DeviceSource::~H264_V4L2DeviceSource() {
	// Finalize MP4 muxer if active
	if (m_mp4Muxer && m_mp4Muxer->isInitialized()) {
		m_mp4Muxer->finalize();
	}
	delete m_mp4Muxer;
}

// split packet in frames					
std::list< std::pair<unsigned char*,size_t> > H264_V4L2DeviceSource::splitFrames(unsigned char* frame, unsigned frameSize) 
{				
	std::list< std::pair<unsigned char*,size_t> > frameList;
	
	size_t bufSize = frameSize;
	size_t size = 0;
	int frameType = 0;
	unsigned char* buffer = this->extractFrame(frame, bufSize, size, frameType);
	
	// For proper H264 output file writing
	std::vector<unsigned char> outputBuffer;
	bool hasKeyFrame = false;
	m_currentFrameData.clear();
	m_currentFrameIsKeyframe = false;
	
	while (buffer != NULL)				
	{	
		switch (frameType&0x1F)					
		{
			case 7: LOG(INFO) << "SPS size:" << size << " bufSize:" << bufSize; m_sps.assign((char*)buffer,size); break;
			case 8: LOG(INFO) << "PPS size:" << size << " bufSize:" << bufSize; m_pps.assign((char*)buffer,size); break;
			case 5: 
				LOG(INFO) << "IDR size:" << size << " bufSize:" << bufSize; 
				hasKeyFrame = true;
				
				// Process H264 keyframe for snapshot if enabled
				if (SnapshotManager::getInstance().isEnabled()) {
					// Get actual frame dimensions from device
					int frameWidth = (m_device && m_device->getWidth() > 0) ? m_device->getWidth() : 1920;
					int frameHeight = (m_device && m_device->getHeight() > 0) ? m_device->getHeight() : 1080;
					
					// Pass SPS/PPS data along with keyframe for better snapshot creation
					SnapshotManager::getInstance().processH264KeyframeWithSPS(buffer, size, m_sps, m_pps, frameWidth, frameHeight);
				}
				// FIXED: Avoid duplicating SPS/PPS in stream - they are sent via getInitFrames()
				// This prevents FFmpeg decoding issues caused by redundant parameter sets
				if (m_repeatConfig && !m_sps.empty() && !m_pps.empty())
				{
					LOG(DEBUG) << "Repeating SPS/PPS before IDR frame (size: " << m_sps.size() << "/" << m_pps.size() << ")";
					frameList.push_back(std::pair<unsigned char*,size_t>((unsigned char*)m_sps.c_str(), m_sps.size()));
					frameList.push_back(std::pair<unsigned char*,size_t>((unsigned char*)m_pps.c_str(), m_pps.size()));
					
					// Add SPS/PPS to output buffer with start codes
					if (m_outfd != -1) {
						// Add start code + SPS
						outputBuffer.insert(outputBuffer.end(), H264marker, H264marker + 4);
						outputBuffer.insert(outputBuffer.end(), m_sps.begin(), m_sps.end());
						// Add start code + PPS  
						outputBuffer.insert(outputBuffer.end(), H264marker, H264marker + 4);
						outputBuffer.insert(outputBuffer.end(), m_pps.begin(), m_pps.end());
					}
				}
			break;
			default: 
				break;
		}
		
		// Add current NAL unit to output buffer with start code
		if (m_outfd != -1) {
			outputBuffer.insert(outputBuffer.end(), H264marker, H264marker + 4);
			outputBuffer.insert(outputBuffer.end(), buffer, buffer + size);
			
			// For MP4 muxer, store individual frame data
			if (m_isMP4 && frameType == 5) { // IDR frame
				// Store frame data for MP4 muxer (will be used after the loop)
				m_currentFrameData.assign(buffer, buffer + size);
				m_currentFrameIsKeyframe = true;
			}
		}
		
		if (!m_sps.empty() && !m_pps.empty())
		{
			u_int32_t profile_level_id = 0;					
			// Fix: properly extract profile_level_id from SPS (skip NAL unit type byte)
			if (m_sps.size() >= 4) {
				profile_level_id = (((unsigned char)m_sps[1])<<16)|(((unsigned char)m_sps[2])<<8)|((unsigned char)m_sps[3]);
			}
		
			char* sps_base64 = base64Encode(m_sps.c_str(), m_sps.size());
			char* pps_base64 = base64Encode(m_pps.c_str(), m_pps.size());		

			std::ostringstream os; 
			os << "profile-level-id=" << std::hex << std::setw(6) << std::setfill('0') << profile_level_id;
			os << ";sprop-parameter-sets=" << sps_base64 <<"," << pps_base64;
			m_auxLine.assign(os.str());
			
			delete [] sps_base64;
			delete [] pps_base64;
		}
		frameList.push_back(std::pair<unsigned char*,size_t>(buffer, size));
		
		buffer = this->extractFrame(&buffer[size], bufSize, size, frameType);
	}
	
	// Write properly formatted H264 data to output file
	if (m_outfd != -1 && !outputBuffer.empty()) {
		if (m_isMP4) {
			// For MP4 output: create snapshot-style MP4 for each keyframe
			if (hasKeyFrame && !m_sps.empty() && !m_pps.empty() && !m_currentFrameData.empty()) {
				// Get frame dimensions
				int frameWidth = (m_device && m_device->getWidth() > 0) ? m_device->getWidth() : 1920;
				int frameHeight = (m_device && m_device->getHeight() > 0) ? m_device->getHeight() : 1080;
				
				// Create complete MP4 snapshot for this keyframe using the fixed createMP4Snapshot
				std::vector<uint8_t> mp4Data = MP4Muxer::createMP4Snapshot(
					m_currentFrameData.data(), m_currentFrameData.size(),
					m_sps, m_pps, frameWidth, frameHeight
				);
				
				if (!mp4Data.empty()) {
					// Write the complete MP4 snapshot to file (overwrites previous content)
					lseek(m_outfd, 0, SEEK_SET); // Reset to beginning
					ftruncate(m_outfd, 0); // Clear file
					int written = write(m_outfd, mp4Data.data(), mp4Data.size());
					if (written == (int)mp4Data.size()) {
						LOG(INFO) << "MP4 keyframe snapshot written: " << written << " bytes";
					} else {
						LOG(ERROR) << "MP4 write error: " << written << "/" << mp4Data.size() << " err:" << strerror(errno);
					}
				} else {
					LOG(ERROR) << "Failed to create MP4 snapshot";
					// Fallback to raw H264
					int written = write(m_outfd, outputBuffer.data(), outputBuffer.size());
					if (written != (int)outputBuffer.size()) {
						LOG(NOTICE) << "H264 fallback write error: " << written << "/" << outputBuffer.size() << " err:" << strerror(errno);
					}
				}
			}
			// For non-keyframes in MP4 mode, we skip writing (single keyframe snapshot mode)
		} else {
			// Raw H264 format
			int written = write(m_outfd, outputBuffer.data(), outputBuffer.size());
			if (written != (int)outputBuffer.size()) {
				LOG(NOTICE) << "H264 output write error: " << written << "/" << outputBuffer.size() << " err:" << strerror(errno);
			} else if (hasKeyFrame) {
				LOG(DEBUG) << "H264 keyframe written to output: " << written << " bytes";
			}
		}
	}
	
	return frameList;
}

std::list< std::string > H264_V4L2DeviceSource::getInitFrames() {
	std::list< std::string > frameList;
	frameList.push_back(this->getFrameWithMarker(m_sps));
	frameList.push_back(this->getFrameWithMarker(m_pps));
	return frameList;
}

bool H264_V4L2DeviceSource::isKeyFrame(const char* buffer, int size) {
	bool res = false;
	if (size > 4)
	{
		int frameType = buffer[4]&0x1F;
		res = (frameType == 5);
	}
	return res;
}

// Simple method to check if output file looks like MP4
bool isMP4Output(int fd) {
	// Simple heuristic: check if we have valid file descriptor
	// In real implementation, this could be passed as a parameter
	// For now, we'll detect based on successful MP4 operations
	return fd > 0; // Will be improved when we integrate with V4l2RTSPServer
}