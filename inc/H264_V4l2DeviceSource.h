/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** H264_V4l2DeviceSource.h
** 
** H264 V4L2 live555 source 
**
** -------------------------------------------------------------------------*/

#pragma once

// project
#include "H26x_V4l2DeviceSource.h"
#include "SnapshotManager.h"

class MP4Muxer; // Forward declaration

// ---------------------------------
// H264 V4L2 FramedSource
// ---------------------------------
// Note: H264marker and H264shortmarker are defined in H26x_V4l2DeviceSource.h

class H264_V4L2DeviceSource : public H26X_V4L2DeviceSource
{
	public:
		static H264_V4L2DeviceSource* createNew(UsageEnvironment& env, DeviceInterface * device, int outputFd, unsigned int queueSize, CaptureMode captureMode, bool repeatConfig, bool keepMarker) {
			return new H264_V4L2DeviceSource(env, device, outputFd, queueSize, captureMode, repeatConfig, keepMarker);
		}

	protected:
		H264_V4L2DeviceSource(UsageEnvironment& env, DeviceInterface * device, int outputFd, unsigned int queueSize, CaptureMode captureMode, bool repeatConfig, bool keepMarker) 
			: H26X_V4L2DeviceSource(env, device, outputFd, queueSize, captureMode, repeatConfig, keepMarker), m_mp4Muxer(nullptr) {
			// Check if output file is MP4 based on file descriptor (simple heuristic)
			// This could be improved by passing a flag from the caller
		}

		virtual ~H264_V4L2DeviceSource();

		// overide V4L2DeviceSource
		virtual std::list< std::pair<unsigned char*,size_t> > splitFrames(unsigned char* frame, unsigned frameSize);
		virtual std::list< std::string > getInitFrames();
		virtual bool isKeyFrame(const char*, int);
		
	private:
		MP4Muxer* m_mp4Muxer;
		void initMP4MuxerIfNeeded();
};
