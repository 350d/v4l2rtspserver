/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** DeviceSource.h
** 
**  live555 source 
**
** -------------------------------------------------------------------------*/


#pragma once


#include "DeviceInterface.h"
#include "V4l2Capture.h"

// -----------------------------------------
//    Video Device Capture Interface 
// -----------------------------------------
class VideoCaptureAccess : public DeviceInterface
{
	public:
		VideoCaptureAccess(V4l2Capture* device) : m_device(device) {}
		virtual ~VideoCaptureAccess()                              { delete m_device; }
			
		virtual size_t read(char* buffer, size_t bufferSize)       { return m_device->read(buffer, bufferSize); }
		virtual int getFd()                                        { return m_device->getFd(); }
		virtual unsigned long getBufferSize()                      { return m_device->getBufferSize(); }
		virtual int getWidth()                                     { return m_device->getWidth(); }
		virtual int getHeight()                                    { return m_device->getHeight(); }
		virtual int getFps()                                       { 
			// Try to get FPS from device, fallback to 30 if not available
			try {
				return m_device->getFps(); 
			} catch (...) {
				return 30; // Default fallback for older libv4l2cpp versions
			}
		}
		virtual int getVideoFormat()                               { return m_device->getFormat(); }
			
	protected:
		V4l2Capture* m_device;
};
