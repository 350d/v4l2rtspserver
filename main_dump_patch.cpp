// Добавить в main.cpp после инициализации устройства:

// В функции инициализации V4L2 устройства:
void dumpV4L2DeviceInfo(int fd, const std::string& deviceName) {
    // Получаем capabilities
    struct v4l2_capability caps;
    if (ioctl(fd, VIDIOC_QUERYCAP, &caps) == 0) {
        SnapshotManager::dumpV4L2Capabilities(caps);
    }
    
    // Получаем текущий формат
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd, VIDIOC_G_FMT, &fmt) == 0) {
        SnapshotManager::dumpPixelFormat(fmt);
        SnapshotManager::dumpDeviceInfo(deviceName, 
                                       fmt.fmt.pix.width, 
                                       fmt.fmt.pix.height,
                                       fmt.fmt.pix.pixelformat, 
                                       30); // FPS
    }
}

// В функции обработки H.264 stream:
void processH264Stream(const uint8_t* data, size_t size) {
    static int frameCount = 0;
    static int iFrames = 0, pFrames = 0, bFrames = 0;
    
    frameCount++;
    
    // Анализируем тип кадра
    std::string frameType = "unknown";
    if (size > 4) {
        // Ищем NAL units
        for (size_t i = 0; i < size - 4; ++i) {
            if (data[i] == 0x00 && data[i+1] == 0x00 && 
                data[i+2] == 0x00 && data[i+3] == 0x01) {
                if (i + 4 < size) {
                    uint8_t nalType = data[i+4] & 0x1F;
                    switch (nalType) {
                        case 5: 
                            frameType = "IDR"; 
                            iFrames++; 
                            break;
                        case 1: 
                            frameType = "P"; 
                            pFrames++; 
                            break;
                        case 7: 
                            frameType = "SPS"; 
                            break;
                        case 8: 
                            frameType = "PPS"; 
                            break;
                        case 6: 
                            frameType = "SEI"; 
                            break;
                    }
                    break;
                }
            }
        }
    }
    
    // Дампим кадр
    std::vector<uint8_t> frameData(data, data + size);
    SnapshotManager::dumpFrameData(frameData, frameType);
    
    // Дампим статистику каждые 100 кадров
    if (frameCount % 100 == 0) {
        SnapshotManager::dumpStreamStatistics(frameCount, iFrames, pFrames, 0);
    }
}
