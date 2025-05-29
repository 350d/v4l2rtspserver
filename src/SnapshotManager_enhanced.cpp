// ENHANCED SnapshotManager с полным дампингом данных
#include "SnapshotManager.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <ctime>

class EnhancedDumper {
private:
    std::string dumpDir;
    int frameCounter;
    bool initialized;
    
public:
    EnhancedDumper() : frameCounter(0), initialized(false) {
        // Создаем директорию для дампа
        time_t now = time(0);
        struct tm* timeinfo = localtime(&now);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "enhanced_dump_%Y%m%d_%H%M%S", timeinfo);
        dumpDir = std::string("tmp/") + buffer;
        
        // Создаем директорию
        std::string cmd = "mkdir -p " + dumpDir;
        system(cmd.c_str());
        
        LOG(NOTICE) << "Enhanced dumper initialized: " << dumpDir;
    }
    
    // Дамп инициализации устройства
    void dumpDeviceInit(const std::string& deviceName, int width, int height, 
                       int pixelFormat, int fps) {
        if (!initialized) {
            std::ofstream file(dumpDir + "/device_init.txt");
            file << "=== DEVICE INITIALIZATION DUMP ===" << std::endl;
            file << "Device: " << deviceName << std::endl;
            file << "Resolution: " << width << "x" << height << std::endl;
            file << "Pixel Format: 0x" << std::hex << pixelFormat << std::dec;
            
            // Декодируем pixel format
            char fourcc[5] = {0};
            fourcc[0] = pixelFormat & 0xFF;
            fourcc[1] = (pixelFormat >> 8) & 0xFF;
            fourcc[2] = (pixelFormat >> 16) & 0xFF;
            fourcc[3] = (pixelFormat >> 24) & 0xFF;
            file << " (" << fourcc << ")" << std::endl;
            
            file << "FPS: " << fps << std::endl;
            file << "Timestamp: " << time(nullptr) << std::endl;
            file.close();
            
            initialized = true;
            LOG(NOTICE) << "Device init dumped: " << deviceName;
        }
    }
    
    // Дамп V4L2 capabilities
    void dumpV4L2Caps(const v4l2_capability& caps) {
        std::ofstream file(dumpDir + "/v4l2_capabilities.txt");
        file << "=== V4L2 CAPABILITIES DUMP ===" << std::endl;
        file << "Driver: " << caps.driver << std::endl;
        file << "Card: " << caps.card << std::endl;
        file << "Bus Info: " << caps.bus_info << std::endl;
        file << "Version: " << caps.version << std::endl;
        file << "Capabilities: 0x" << std::hex << caps.capabilities << std::dec << std::endl;
        file << "Device Caps: 0x" << std::hex << caps.device_caps << std::dec << std::endl;
        file.close();
        
        LOG(NOTICE) << "V4L2 capabilities dumped";
    }
    
    // Дамп формата пикселей
    void dumpPixelFormat(const v4l2_format& fmt) {
        std::ofstream file(dumpDir + "/pixel_format.txt");
        file << "=== PIXEL FORMAT DUMP ===" << std::endl;
        file << "Type: " << fmt.type << std::endl;
        file << "Width: " << fmt.fmt.pix.width << std::endl;
        file << "Height: " << fmt.fmt.pix.height << std::endl;
        file << "Pixel Format: 0x" << std::hex << fmt.fmt.pix.pixelformat << std::dec;
        
        char fourcc[5] = {0};
        fourcc[0] = fmt.fmt.pix.pixelformat & 0xFF;
        fourcc[1] = (fmt.fmt.pix.pixelformat >> 8) & 0xFF;
        fourcc[2] = (fmt.fmt.pix.pixelformat >> 16) & 0xFF;
        fourcc[3] = (fmt.fmt.pix.pixelformat >> 24) & 0xFF;
        file << " (" << fourcc << ")" << std::endl;
        
        file << "Bytes per line: " << fmt.fmt.pix.bytesperline << std::endl;
        file << "Size image: " << fmt.fmt.pix.sizeimage << std::endl;
        file << "Colorspace: " << fmt.fmt.pix.colorspace << std::endl;
        file.close();
        
        LOG(NOTICE) << "Pixel format dumped";
    }
    
    // Дамп H.264 параметров
    void dumpH264Params(const std::vector<uint8_t>& sps, const std::vector<uint8_t>& pps) {
        if (!sps.empty()) {
            std::ofstream spsFile(dumpDir + "/h264_sps.bin", std::ios::binary);
            spsFile.write(reinterpret_cast<const char*>(sps.data()), sps.size());
            spsFile.close();
            
            // Также в hex формате
            std::ofstream spsHex(dumpDir + "/h264_sps.hex");
            for (size_t i = 0; i < sps.size(); ++i) {
                spsHex << std::hex << std::setw(2) << std::setfill('0') << (int)sps[i];
                if (i < sps.size() - 1) spsHex << " ";
            }
            spsHex.close();
            
            LOG(NOTICE) << "SPS dumped: " << sps.size() << " bytes";
        }
        
        if (!pps.empty()) {
            std::ofstream ppsFile(dumpDir + "/h264_pps.bin", std::ios::binary);
            ppsFile.write(reinterpret_cast<const char*>(pps.data()), pps.size());
            ppsFile.close();
            
            // Также в hex формате
            std::ofstream ppsHex(dumpDir + "/h264_pps.hex");
            for (size_t i = 0; i < pps.size(); ++i) {
                ppsHex << std::hex << std::setw(2) << std::setfill('0') << (int)pps[i];
                if (i < pps.size() - 1) ppsHex << " ";
            }
            ppsHex.close();
            
            LOG(NOTICE) << "PPS dumped: " << pps.size() << " bytes";
        }
    }
    
    // Дамп кадра с анализом типа
    void dumpFrame(const std::vector<uint8_t>& frameData, const std::string& frameType) {
        frameCounter++;
        
        std::string filename = dumpDir + "/frame_" + std::to_string(frameCounter) + 
                              "_" + frameType + ".bin";
        std::ofstream file(filename, std::ios::binary);
        file.write(reinterpret_cast<const char*>(frameData.data()), frameData.size());
        file.close();
        
        // Анализируем начало кадра
        std::string infoFile = dumpDir + "/frame_" + std::to_string(frameCounter) + 
                              "_" + frameType + "_info.txt";
        std::ofstream info(infoFile);
        info << "=== FRAME " << frameCounter << " INFO ===" << std::endl;
        info << "Type: " << frameType << std::endl;
        info << "Size: " << frameData.size() << " bytes" << std::endl;
        info << "Timestamp: " << time(nullptr) << std::endl;
        
        if (frameData.size() >= 16) {
            info << "Header (hex): ";
            for (int i = 0; i < 16; ++i) {
                info << std::hex << std::setw(2) << std::setfill('0') << (int)frameData[i] << " ";
            }
            info << std::endl;
            
            // Анализируем NAL unit type если это H.264
            if (frameData.size() > 4) {
                // Ищем start code и NAL unit
                for (size_t i = 0; i < frameData.size() - 4; ++i) {
                    if (frameData[i] == 0x00 && frameData[i+1] == 0x00 && 
                        frameData[i+2] == 0x00 && frameData[i+3] == 0x01) {
                        if (i + 4 < frameData.size()) {
                            uint8_t nalType = frameData[i+4] & 0x1F;
                            info << "NAL Unit Type: " << (int)nalType;
                            switch (nalType) {
                                case 1: info << " (Non-IDR slice)"; break;
                                case 5: info << " (IDR slice)"; break;
                                case 6: info << " (SEI)"; break;
                                case 7: info << " (SPS)"; break;
                                case 8: info << " (PPS)"; break;
                                case 9: info << " (Access unit delimiter)"; break;
                                default: info << " (Other)"; break;
                            }
                            info << std::endl;
                            break;
                        }
                    }
                }
            }
        }
        info.close();
        
        LOG(NOTICE) << "Frame " << frameCounter << " (" << frameType << ") dumped: " 
                   << frameData.size() << " bytes";
    }
    
    // Дамп SEI данных
    void dumpSEI(const std::vector<uint8_t>& seiData) {
        if (!seiData.empty()) {
            std::string filename = dumpDir + "/sei_data.bin";
            std::ofstream file(filename, std::ios::binary);
            file.write(reinterpret_cast<const char*>(seiData.data()), seiData.size());
            file.close();
            
            LOG(NOTICE) << "SEI data dumped: " << seiData.size() << " bytes";
        }
    }
    
    // Дамп статистики стрима
    void dumpStreamStats(int totalFrames, int iFrames, int pFrames, int bFrames) {
        std::ofstream file(dumpDir + "/stream_stats.txt");
        file << "=== STREAM STATISTICS ===" << std::endl;
        file << "Total Frames: " << totalFrames << std::endl;
        file << "I-Frames: " << iFrames << std::endl;
        file << "P-Frames: " << pFrames << std::endl;
        file << "B-Frames: " << bFrames << std::endl;
        file << "I-Frame Ratio: " << (float)iFrames / totalFrames * 100 << "%" << std::endl;
        file.close();
        
        LOG(NOTICE) << "Stream statistics dumped";
    }
    
    std::string getDumpDir() const { return dumpDir; }
};

// Глобальный экземпляр dumper
static EnhancedDumper g_dumper;

// Модифицированные функции SnapshotManager
void SnapshotManager::dumpDeviceInfo(const std::string& device, int width, int height, 
                                    int pixelFormat, int fps) {
    g_dumper.dumpDeviceInit(device, width, height, pixelFormat, fps);
}

void SnapshotManager::dumpV4L2Capabilities(const v4l2_capability& caps) {
    g_dumper.dumpV4L2Caps(caps);
}

void SnapshotManager::dumpPixelFormat(const v4l2_format& fmt) {
    g_dumper.dumpPixelFormat(fmt);
}

void SnapshotManager::dumpH264Parameters(const std::vector<uint8_t>& sps, 
                                        const std::vector<uint8_t>& pps) {
    g_dumper.dumpH264Params(sps, pps);
}

void SnapshotManager::dumpFrameData(const std::vector<uint8_t>& frameData, 
                                   const std::string& frameType) {
    g_dumper.dumpFrame(frameData, frameType);
}

void SnapshotManager::dumpSEIData(const std::vector<uint8_t>& seiData) {
    g_dumper.dumpSEI(seiData);
}

void SnapshotManager::dumpStreamStatistics(int total, int i, int p, int b) {
    g_dumper.dumpStreamStats(total, i, p, b);
}

std::string SnapshotManager::getDumpDirectory() {
    return g_dumper.getDumpDir();
}
