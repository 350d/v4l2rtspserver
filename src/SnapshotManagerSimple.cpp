/* ---------------------------------------------------------------------------
** This software is in the public domain, furnished "as is", without technical
** support, and with no warranty, express or implied, as to its usefulness for
** any purpose.
**
** SnapshotManagerSimple.cpp
** 
** Simplified Real Image Snapshot Manager implementation
** Optimized for Pi Zero with modern camera stack
**
** -------------------------------------------------------------------------*/

#include "SnapshotManagerSimple.h"
#include "logger.h"
#include <cstring>
#include <fstream>
#include <algorithm>
#include <cstdlib>

SnapshotManagerSimple::SnapshotManagerSimple() {
    LOG(INFO) << "SnapshotManagerSimple initialized";
}

void SnapshotManagerSimple::setFrameDimensions(int width, int height) {
    m_width = width;
    m_height = height;
    LOG(DEBUG) << "Frame dimensions set to: " << m_width << "x" << m_height;
}

void SnapshotManagerSimple::setSaveInterval(int intervalSeconds) {
    // Validate range: 1-60 seconds
    if (intervalSeconds < 1) {
        intervalSeconds = 1;
        LOG(WARN) << "Save interval too low, set to minimum: 1 second";
    } else if (intervalSeconds > 60) {
        intervalSeconds = 60;
        LOG(WARN) << "Save interval too high, set to maximum: 60 seconds";
    }
    
    m_saveInterval = intervalSeconds;
    LOG(INFO) << "Snapshot save interval set to: " << m_saveInterval << " seconds";
}

bool SnapshotManagerSimple::initialize(int width, int height) {
    m_width = width;
    m_height = height;
    
    if (!m_enabled) {
        m_mode = SnapshotMode::DISABLED;
        LOG(INFO) << "SnapshotManager disabled";
        return true;
    }
    
    // Start in H264 fallback mode, will switch automatically based on data
    m_mode = SnapshotMode::H264_FALLBACK;
    LOG(NOTICE) << "SnapshotManagerSimple initialized in auto-detection mode";
    return true;
}

void SnapshotManagerSimple::processMJPEGFrame(const unsigned char* jpegData, size_t dataSize) {
    if (!m_enabled || !jpegData || dataSize == 0) {
        return;
    }
    
    // Real JPEG data from MJPEG stream
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_currentSnapshot.assign(jpegData, jpegData + dataSize);
    m_lastSnapshotTime = std::time(nullptr);
    
    // Update mode to MJPEG stream
    if (m_mode != SnapshotMode::MJPEG_STREAM) {
        m_mode = SnapshotMode::MJPEG_STREAM;
        LOG(NOTICE) << "Switched to MJPEG stream mode (real images)";
    }
    
    LOG(DEBUG) << "Real MJPEG snapshot captured: " << dataSize << " bytes";
    
    // Auto-save if file path is specified
    autoSaveSnapshot();
}

void SnapshotManagerSimple::processH264Keyframe(const unsigned char* h264Data, size_t dataSize, int width, int height) {
    if (!m_enabled || !h264Data || dataSize == 0) {
        return;
    }
    
    // Create informational SVG for H264 streams
    createH264InfoSnapshot(dataSize, width, height);
}

void SnapshotManagerSimple::processRawFrame(const unsigned char* yuvData, size_t dataSize, int width, int height) {
    if (!m_enabled || !yuvData || dataSize == 0) {
        return;
    }
    
    // Try YUV->JPEG conversion for real snapshots
    if (width > 0 && height > 0) {
        if (convertYUVToJPEG(yuvData, dataSize, width, height)) {
            return; // Successfully converted YUV to image
        }
    }
    
    // Fallback to info snapshot
    createH264InfoSnapshot(dataSize, width, height);
}

void SnapshotManagerSimple::createH264InfoSnapshot(size_t h264Size, int width, int height) {
    // Create informational SVG for H264/YUV streams
    int actualWidth = width > 0 ? width : m_width;
    int actualHeight = height > 0 ? height : m_height;
    
    std::time_t now = std::time(nullptr);
    char timeStr[100];
    std::strftime(timeStr, sizeof(timeStr), "%H:%M:%S", std::localtime(&now));
    
    std::string svgContent = 
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg width=\"400\" height=\"300\" xmlns=\"http://www.w3.org/2000/svg\">\n"
        "  <rect width=\"100%\" height=\"100%\" fill=\"#f8f9fa\"/>\n"
        "  <rect x=\"10\" y=\"10\" width=\"380\" height=\"280\" fill=\"#ffffff\" stroke=\"#dee2e6\" stroke-width=\"2\" rx=\"8\"/>\n"
        "  <text x=\"200\" y=\"50\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"20\" font-weight=\"bold\" fill=\"#495057\">Stream Active</text>\n"
        "  <text x=\"200\" y=\"100\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"14\" fill=\"#6c757d\">Resolution: " + 
        std::to_string(actualWidth) + "x" + std::to_string(actualHeight) + "</text>\n"
        "  <text x=\"200\" y=\"130\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"14\" fill=\"#6c757d\">Data: " + 
        std::to_string(h264Size) + " bytes</text>\n"
        "  <text x=\"200\" y=\"160\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"14\" fill=\"#6c757d\">Time: " + 
        std::string(timeStr) + "</text>\n"
        "  <rect x=\"50\" y=\"180\" width=\"300\" height=\"80\" fill=\"#e9ecef\" stroke=\"#adb5bd\" stroke-width=\"1\" rx=\"4\"/>\n"
        "  <text x=\"200\" y=\"200\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"12\" fill=\"#495057\">Simplified Snapshot Mode</text>\n"
        "  <text x=\"200\" y=\"220\" text-anchor=\"middle\" font-family=\"Arial\" font-size=\"11\" fill=\"#6c757d\">For real images: use MJPEG format</text>\n"
        "  <text x=\"200\" y=\"240\" text-anchor=\"middle\" font-family=\"monospace\" font-size=\"9\" fill=\"#868e96\">or YUV data will be converted</text>\n"
        "  <text x=\"200\" y=\"255\" text-anchor=\"middle\" font-family=\"monospace\" font-size=\"9\" fill=\"#868e96\">v4l2-ctl --set-fmt-video=pixelformat=MJPG</text>\n"
        "</svg>";
    
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    m_currentSnapshot.assign(svgContent.begin(), svgContent.end());
    m_lastSnapshotTime = std::time(nullptr);
    
    // Update mode to H264 fallback
    if (m_mode != SnapshotMode::H264_FALLBACK) {
        m_mode = SnapshotMode::H264_FALLBACK;
        LOG(NOTICE) << "Switched to H264 fallback mode (info graphics)";
    }
    
    LOG(DEBUG) << "H264 fallback snapshot (SVG) created: " << m_currentSnapshot.size() << " bytes";
    
    // Auto-save if file path is specified
    autoSaveSnapshot();
}

bool SnapshotManagerSimple::convertYUVToJPEG(const unsigned char* yuvData, size_t dataSize, int width, int height) {
    // Simple YUV422 (YUYV) to PPM conversion
    if (!yuvData || dataSize == 0 || width <= 0 || height <= 0) {
        return false;
    }
    
    // Calculate expected data size for YUYV format (2 bytes per pixel)
    size_t expectedSize = width * height * 2;
    if (dataSize < expectedSize) {
        LOG(DEBUG) << "YUV data too small: " << dataSize << " expected: " << expectedSize;
        return false;
    }
    
    try {
        // Convert YUYV to RGB
        std::vector<unsigned char> rgbData;
        rgbData.reserve(width * height * 3);
        
        for (int i = 0; i < width * height * 2; i += 4) {
            if (i + 3 < dataSize) {
                // YUYV format: Y0 U Y1 V
                int y0 = yuvData[i];
                int u = yuvData[i + 1];
                int y1 = yuvData[i + 2];
                int v = yuvData[i + 3];
                
                // Convert to RGB (simplified conversion)
                for (int j = 0; j < 2; j++) {
                    int y = (j == 0) ? y0 : y1;
                    
                    // YUV to RGB conversion
                    int r = y + 1.402 * (v - 128);
                    int g = y - 0.344 * (u - 128) - 0.714 * (v - 128);
                    int b = y + 1.772 * (u - 128);
                    
                    // Clamp values
                    r = std::max(0, std::min(255, r));
                    g = std::max(0, std::min(255, g));
                    b = std::max(0, std::min(255, b));
                    
                    rgbData.push_back(r);
                    rgbData.push_back(g);
                    rgbData.push_back(b);
                }
            }
        }
        
        // Create PPM format (browsers can display this)
        std::string ppmHeader = "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
        
        std::vector<unsigned char> ppmData;
        ppmData.insert(ppmData.end(), ppmHeader.begin(), ppmHeader.end());
        ppmData.insert(ppmData.end(), rgbData.begin(), rgbData.end());
        
        // Store as snapshot
        {
            std::lock_guard<std::mutex> lock(m_snapshotMutex);
            m_currentSnapshot = ppmData;
            m_lastSnapshotTime = std::time(nullptr);
            
            // Update mode to YUV converted
            if (m_mode != SnapshotMode::YUV_CONVERTED) {
                m_mode = SnapshotMode::YUV_CONVERTED;
                LOG(NOTICE) << "Switched to YUV conversion mode (real images from YUV)";
            }
        }
        
        LOG(DEBUG) << "YUV->PPM conversion successful: " << ppmData.size() << " bytes (" 
                   << width << "x" << height << ")";
        
        // Auto-save if file path is specified
        autoSaveSnapshot();
        
        return true;
        
    } catch (const std::exception& e) {
        LOG(ERROR) << "YUV->PPM conversion failed: " << e.what();
        return false;
    }
}

bool SnapshotManagerSimple::getSnapshot(std::vector<unsigned char>& data) {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    
    if (m_currentSnapshot.empty()) {
        return false;
    }
    
    data = m_currentSnapshot;
    return true;
}

std::string SnapshotManagerSimple::getSnapshotMimeType() const {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    
    // Check actual content type if we have data
    if (!m_currentSnapshot.empty()) {
        // Check for JPEG magic bytes (FF D8 FF)
        if (m_currentSnapshot.size() >= 3 && 
            m_currentSnapshot[0] == 0xFF && 
            m_currentSnapshot[1] == 0xD8 && 
            m_currentSnapshot[2] == 0xFF) {
            return "image/jpeg";
        }
        
        // Check for PPM format (starts with "P6")
        if (m_currentSnapshot.size() >= 2 && 
            m_currentSnapshot[0] == 'P' && 
            m_currentSnapshot[1] == '6') {
            return "image/x-portable-pixmap";
        }
        
        // Check for SVG content (starts with "<?xml" or "<svg")
        if (m_currentSnapshot.size() >= 5) {
            std::string start(m_currentSnapshot.begin(), m_currentSnapshot.begin() + 5);
            if (start == "<?xml" || start == "<svg ") {
                return "image/svg+xml";
            }
        }
    }
    
    // Fallback to mode-based detection
    switch (m_mode) {
        case SnapshotMode::MJPEG_STREAM:
            return "image/jpeg";
        case SnapshotMode::YUV_CONVERTED:
            return "image/x-portable-pixmap";
        case SnapshotMode::H264_FALLBACK:
            return "image/svg+xml";
        default:
            return "text/plain";
    }
}

std::string SnapshotManagerSimple::getModeDescription() const {
    switch (m_mode) {
        case SnapshotMode::DISABLED:
            return "Disabled";
        case SnapshotMode::MJPEG_STREAM:
            return "MJPEG Stream (real JPEG images)";
        case SnapshotMode::YUV_CONVERTED:
            return "YUV Converted (real images from YUV data)";
        case SnapshotMode::H264_FALLBACK:
            return "H264 Fallback (info graphics)";
        default:
            return "Unknown";
    }
}

bool SnapshotManagerSimple::hasRecentSnapshot() const {
    std::lock_guard<std::mutex> lock(m_snapshotMutex);
    if (m_lastSnapshotTime == 0) {
        return false;
    }
    
    // Consider snapshot recent if it's less than 30 seconds old
    std::time_t now = std::time(nullptr);
    return (now - m_lastSnapshotTime) < 30;
}

void SnapshotManagerSimple::autoSaveSnapshot() {
    if (m_filePath.empty()) {
        return;
    }
    
    // Check save interval
    std::time_t now = std::time(nullptr);
    if (m_lastSaveTime > 0 && (now - m_lastSaveTime) < m_saveInterval) {
        // Too soon to save again
        return;
    }
    
    std::vector<unsigned char> dataToSave;
    {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        dataToSave = m_currentSnapshot;
    }
    
    if (dataToSave.empty()) {
        return;
    }
    
    try {
        std::ofstream file(m_filePath, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(dataToSave.data()), dataToSave.size());
            file.close();
            if (file.good()) {
                m_lastSaveTime = now; // Update save time only on successful save
                LOG(DEBUG) << "Auto-saved snapshot: " << m_filePath << " (" << dataToSave.size() << " bytes)";
            } else {
                LOG(ERROR) << "Error writing snapshot to file: " << m_filePath;
            }
        } else {
            LOG(ERROR) << "Failed to open file for writing: " << m_filePath;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception while auto-saving snapshot: " << e.what();
    } catch (...) {
        LOG(ERROR) << "Unknown exception while auto-saving snapshot";
    }
}

bool SnapshotManagerSimple::saveSnapshotToFile() {
    if (m_filePath.empty()) {
        LOG(ERROR) << "No file path specified for snapshot saving";
        return false;
    }
    return saveSnapshotToFile(m_filePath);
}

bool SnapshotManagerSimple::saveSnapshotToFile(const std::string& filePath) {
    if (!m_enabled) {
        LOG(WARN) << "Snapshots are disabled";
        return false;
    }
    
    std::vector<unsigned char> snapshotData;
    if (!getSnapshot(snapshotData) || snapshotData.empty()) {
        LOG(WARN) << "No snapshot data available for saving";
        return false;
    }
    
    try {
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            LOG(ERROR) << "Failed to open file for writing: " << filePath;
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(snapshotData.data()), snapshotData.size());
        file.close();
        
        if (file.good()) {
            LOG(NOTICE) << "Snapshot saved to file: " << filePath << " (" << snapshotData.size() << " bytes)";
            return true;
        } else {
            LOG(ERROR) << "Error writing snapshot to file: " << filePath;
            return false;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception while saving snapshot: " << e.what();
        return false;
    }
} 