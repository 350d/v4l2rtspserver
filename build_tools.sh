#!/bin/bash

echo "Building v4l2rtspserver snapshot tools..."

# Build device inspector utility
echo "Building device_inspector..."
if g++ -o device_inspector src/device_inspector.cpp -std=c++11; then
    echo "✓ device_inspector built successfully"
else
    echo "✗ Failed to build device_inspector"
    exit 1
fi

# Make it executable
chmod +x device_inspector

echo ""
echo "Tools built successfully!"
echo ""
echo "Usage:"
echo "  ./device_inspector          - Scan all video devices"
echo "  ./device_inspector /dev/video0  - Inspect specific device"
echo ""
echo "This tool will help you determine:"
echo "  - Available video devices"
echo "  - Supported formats (H264, MJPEG, etc.)"
echo "  - Whether device supports multiple simultaneous connections"
echo "  - Related devices for the same camera"
echo "" 