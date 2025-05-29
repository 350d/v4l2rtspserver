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

// ---------------------------------
// H264 V4L2 FramedSource
// ---------------------------------


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
					// Pass SPS/PPS data along with keyframe for better snapshot creation
					SnapshotManager::getInstance().processH264KeyframeWithSPS(buffer, size, m_sps, m_pps, 0, 0);
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
		int written = write(m_outfd, outputBuffer.data(), outputBuffer.size());
		if (written != (int)outputBuffer.size()) {
			LOG(NOTICE) << "H264 output write error: " << written << "/" << outputBuffer.size() << " err:" << strerror(errno);
		} else if (hasKeyFrame) {
			LOG(DEBUG) << "H264 keyframe written to output: " << written << " bytes";
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