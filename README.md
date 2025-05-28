[![CircleCI](https://circleci.com/gh/mpromonet/v4l2rtspserver.svg?style=shield)](https://circleci.com/gh/mpromonet/v4l2rtspserver)
[![CirusCI](https://api.cirrus-ci.com/github/mpromonet/v4l2rtspserver.svg?branch=master)](https://cirrus-ci.com/github/mpromonet/v4l2rtspserver)
[![Snap Status](https://snapcraft.io//v4l2-rtspserver/badge.svg)](https://snapcraft.io/v4l2-rtspserver)
[![GithubCI](https://github.com/mpromonet/v4l2rtspserver/workflows/C/C++%20CI/badge.svg)](https://github.com/mpromonet/v4l2rtspserver/actions)

[![Codacy Badge](https://app.codacy.com/project/badge/Grade/4602f447d1c1408d865ebb4ef68f12f1)](https://app.codacy.com/gh/mpromonet/v4l2rtspserver/dashboard?utm_source=gh&utm_medium=referral&utm_content=&utm_campaign=Badge_grade)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/4644/badge.svg)](https://scan.coverity.com/projects/4644)
[![Coverage Status](https://coveralls.io/repos/github/mpromonet/v4l2rtspserver/badge.svg?branch=master)](https://coveralls.io/github/mpromonet/v4l2rtspserver?branch=master)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8853/badge)](https://www.bestpractices.dev/projects/8853)

[![Release](https://img.shields.io/github/release/mpromonet/v4l2rtspserver.svg)](https://github.com/mpromonet/v4l2rtspserver/releases/latest)
[![Download](https://img.shields.io/github/downloads/mpromonet/v4l2rtspserver/total.svg)](https://github.com/mpromonet/v4l2rtspserver/releases/latest)
[![Docker Pulls](https://img.shields.io/docker/pulls/mpromonet/v4l2rtspserver.svg)](https://hub.docker.com/r/mpromonet/v4l2rtspserver/)
[![OpenWrt 24.10 x86_64 package](https://repology.org/badge/version-for-repo/openwrt_24_10_x86_64/v4l2rtspserver.svg)](https://repology.org/project/v4l2rtspserver/versions)

v4l2rtspserver
====================

This is an streamer feed from :
 - an Video4Linux device that support H264, HEVC, JPEG, VP8 or VP9 capture.
 - an ALSA device that support PCM S16_BE, S16_LE, S32_BE or S32_LE
 
The RTSP server support :
- RTP/UDP unicast
- RTP/UDP multicast
- RTP/TCP
- RTP/RTSP/HTTP

The HTTP server support (available using -S option for capture format that could be muxed in Transport Stream):
- HLS
- MPEG-DASH

**New Features:**
- **Real-time snapshots** via `/snapshot` endpoint with intelligent format detection
- **Smart format detection** - automatically selects supported video formats instead of blindly trying HEVC
- **Multi-device camera analysis** with comprehensive device inspector tool

Dependencies
------------
 - liblivemedia-dev [License LGPL](http://www.live555.com/liveMedia/) > live.2012.01.07 (need StreamReplicator)
 - libv4l2cpp [Unlicense](https://github.com/mpromonet/libv4l2cpp/blob/master/LICENSE)
 - liblog4cpp5-dev  [License LGPL](http://log4cpp.sourceforge.net/#license) (optional)
If liblog4cpp5-dev is not present, a simple log using std::cout is used.
 - libasound2-dev Licence LGPL (optional)
If libasound2-dev is not present in the build environment, there will have no audio support.
 - libssl-dev (optional)
If libssl-dev is not present rtsps/srtp will not be available

Usage
-----
	./v4l2rtspserver [-v[v]] [-Q queueSize] [-O file] [-j[filepath]] [-J wxhx[interval]] \
			       [-I interface] [-P RTSP port] [-p RTSP/HTTP port] [-m multicast url] [-u unicast url] [-M multicast addr] [-c] [-t timeout] [-S[duration]] \
			       [-r] [-s] [-W width] [-H height] [-F fps] [-f[format]] [device1] [device2]

**General Options:**
		 -v              : verbose (use -vv for very verbose)
		 -Q <length>     : Number of frame queue (default 10)
		 -O <output>     : Copy captured frame to a file or a V4L2 device
		 -b <webroot>    : path to webroot for HTTP server

**Snapshot Options:**
		 -j [<filepath>] : enable snapshots accessible via /snapshot endpoint, optionally auto-save to file
		 -J <w>x<h>[x<i>]: snapshot resolution and save interval (default 640x480x5, interval 1-60 seconds)
		 
**RTSP/RTP Options:**
		 -I <addr>       : RTSP interface (default autodetect)
		 -P <port>       : RTSP port (default 8554)
		 -p <port>       : RTSP over HTTP port (default 0)
		 -U <user:pass>  : RTSP user and password
		 -R <realm>      : use md5 password 'md5(<username>:<realm>:<password>'
		 -u <url>        : unicast url (default unicast)
		 -m <url>        : multicast url (default multicast)
		 -M <addr>       : multicast group:port (default is random_address:20000)
		 -c              : don't repeat config (recommended for FFmpeg compatibility)
		 -t <secs>       : RTCP expiration timeout (default 65)
		 -S[<duration>]  : enable HLS & MPEG-DASH with segment duration in seconds (default 2)
		 -x <sslkeycert> : enable SRTP
		 -X              : enable RTSPS
 
**V4L2 Options:**
		 -r              : V4L2 capture using read interface (default: memory mapped buffers)
		 -w              : V4L2 capture using write interface (default: memory mapped buffers)
		 -B              : V4L2 capture using blocking mode (default: non-blocking mode)
		 -s              : V4L2 capture using live555 mainloop (default: separated reading thread)
		 -f              : V4L2 capture using current capture format (-W,-H are ignored)
		 -f<format>      : V4L2 capture using format (-W,-H are used)
		 -W <width>      : V4L2 capture width (default 640)
		 -H <height>     : V4L2 capture height (default 480)
		 -F <fps>        : V4L2 capture framerate (default 25, 0 disable setting framerate)
		 -G <w>x<h>[x<f>]: V4L2 capture format (default 0x0x25)
		 
**ALSA Options (if compiled with ALSA support):**
		 -A <freq>       : ALSA capture frequency (default 44100)
		 -C <channels>   : ALSA capture channels (default 2)
		 -a <fmt>        : ALSA capture audio format (default S16_LE)
		 
**Device Specification:**
		 device          : V4L2 capture device and/or ALSA device (default /dev/video0)
		                  Examples: /dev/video0 (video only)
		                           ,default (audio only)  
		                           /dev/video0,default (video + audio)
		                           /dev/video0 /dev/video1 (multiple video devices)

Snapshot Support
----------------
v4l2rtspserver supports real-time image snapshots via HTTP endpoint `/snapshot` when started with `-j` flag.

### Intelligent Snapshot Modes

The system automatically selects the best snapshot mode based on camera capabilities:

1. **MJPEG Stream Mode** - Real JPEG snapshots when camera streams in MJPEG format
2. **MJPEG Device Mode** - Real JPEG snapshots from separate MJPEG device (ideal for dual-format cameras)  
3. **H264 MP4 Mode** - Mini MP4 video snapshots with H264 keyframes (real video content)
4. **H264_FALLBACK** - Real MP4 snapshots with cached I-frames
   - Creates MP4 containers with actual H264 keyframes
   - Caches last I-frame and SPS/PPS data for reuse
   - Uses cached data when no current frame available
   - Provides real video snapshots instead of placeholders
5. **YUV Converted Mode** - Real JPEG images converted from YUV/RAW data

### Basic Usage

```bash
# Enable snapshots via HTTP endpoint only
./v4l2rtspserver -j /dev/video0

# Enable snapshots with automatic file saving
./v4l2rtspserver -j /path/to/snapshot.jpg /dev/video0

# Access snapshots via HTTP
curl http://localhost:8554/snapshot > snapshot.jpg
# or open in browser: http://localhost:8554/snapshot
```

### Advanced Configuration

**Resolution and Save Interval Control:**
```bash
# Custom resolution (width x height)
./v4l2rtspserver -j /tmp/snap.jpg -J 1280x720 /dev/video0

# Custom resolution and save interval (width x height x interval_seconds)
./v4l2rtspserver -j /tmp/snap.jpg -J 1920x1080x2 /dev/video0
```

**Save Frequency Options:**
- **Default interval**: 5 seconds (balanced performance)
- **High frequency**: 1-2 seconds (intensive monitoring)
- **Low frequency**: 10-60 seconds (archival/storage optimization)
- **Range validation**: Automatically clamps to 1-60 seconds

### Practical Examples

```bash
# High-quality monitoring (every 2 seconds)
./v4l2rtspserver -j /var/log/camera.jpg -J 1920x1080x2 /dev/video0

# Balanced general use (every 5 seconds - default)
./v4l2rtspserver -j /tmp/current.jpg -J 1280x720 /dev/video0

# Archive mode (every 60 seconds, maximum interval)
./v4l2rtspserver -j /archive/hourly.jpg -J 1920x1080x60 /dev/video0

# Multi-format support for best compatibility
./v4l2rtspserver -j /tmp/snap.jpg -J 640x480x1 -fH264 -fMJPEG /dev/video0
```

### Multi-Device Camera Support

For cameras creating multiple video devices (e.g., `/dev/video0` for H264, `/dev/video1` for MJPEG):
- System automatically detects related MJPEG devices
- Uses MJPEG device for real image snapshots while streaming H264
- Configurable snapshot resolution independent of stream resolution

### File Auto-Save Features

- **Frequency control**: Respects save interval to prevent disk overload
- **Format auto-detection**: Automatically determines file extension (.jpg, .mp4, .svg)
- **Thread-safe operations**: Non-blocking file writes
- **Error handling**: Graceful fallback on file system errors
- **HTTP always available**: Endpoint returns latest snapshot regardless of save interval

### Docker Usage

```bash
# Enable snapshots in Docker
docker run --device=/dev/video0 -p 8554:8554 -it mpromonet/v4l2rtspserver -j

# Access snapshots
curl http://localhost:8554/snapshot > snapshot.jpg
```

Smart Format Detection
----------------------
The server intelligently detects and prioritizes supported video formats instead of attempting unsupported formats.
This prevents common errors like `"Cannot set pixelformat to:HEVC format is:YU12"` on cameras that don't support HEVC.

### Automatic Format Priority

**Default priority order (if no format specified):**
1. **H264** - Most common and efficient, best compatibility
2. **HEVC/H265** - High quality, if supported by device  
3. **MJPEG** - Good compatibility, useful for snapshots
4. **JPEG** - Basic JPEG format
5. **NV12** - Raw format fallback

### Manual Format Override

```bash
# Force specific format (bypasses auto-detection)
./v4l2rtspserver -fH264 /dev/video0     # Force H264
./v4l2rtspserver -fMJPG /dev/video0     # Force MJPEG
./v4l2rtspserver -fHEVC /dev/video0     # Force HEVC (if supported)

# Multiple format support (try in order)
./v4l2rtspserver -fH264 -fMJPEG /dev/video0
```

### FFmpeg Compatibility Enhancement

For optimal FFmpeg decoding compatibility:
```bash
# Recommended settings for FFmpeg clients
./v4l2rtspserver -c /dev/video0

# Combined with snapshots
./v4l2rtspserver -c -j /tmp/snap.jpg /dev/video0

# Full optimization for FFmpeg + snapshots
./v4l2rtspserver -c -j /tmp/snap.jpg -J 1280x720x5 -fH264 /dev/video0
```

**Why use `-c` flag:**
- Disables SPS/PPS parameter repetition in H264 stream
- Prevents duplicate parameter sets that can confuse FFmpeg
- Improves stream parsing reliability
- Recommended for all FFmpeg-based applications

### Device Format Analysis

Use the device inspector to check format support:
```bash
# Analyze device capabilities
./device_inspector /dev/video0

# Multi-device analysis
./device_inspector /dev/video0 --multi
```

Device Inspector Tool
--------------------
Build and use the device inspector to analyze camera capabilities:

```bash
# Build inspector tool
./build_tools.sh

# Scan all video devices
./device_inspector

# Analyze specific device
./device_inspector /dev/video0

# Comprehensive multi-device testing
./device_inspector /dev/video0 --multi
```

**The inspector reports:**
- Supported video formats (H264, MJPEG, HEVC, etc.)
- Multi-device camera detection
- Concurrent access capabilities  
- Optimal configuration recommendations
- Device compatibility matrix

When audio support is not present, ALSA options are not printed running with `-h` argument.

Authentification is enable when almost one user is defined. You can configure credentials :
 * using plain text password: 
 
       -U foo:bar -U admin:admin
 * using md5 password: 
 
       -R myrealm -U foo:$(echo -n foo:myrealm:bar | md5sum | cut -d- -f1) -U admin:$(echo -n admin:myrealm:admin | md5sum | cut -d- -f1)

It is possible to compose the RTSP session is different ways :
 * v4l2rtspserver /dev/video0              : one RTSP session with RTP video capturing V4L2 device /dev/video0
 * v4l2rtspserver ,default                 : one RTSP session with RTP audio capturing ALSA device default
 * v4l2rtspserver /dev/video0,default      : one RTSP session with RTP audio and RTP video
 * v4l2rtspserver /dev/video0 ,default     : two RTSP sessions first one with RTP video and second one with RTP audio
 * v4l2rtspserver /dev/video0 /dev/video1  : two RTSP sessions with an RTP video
 * v4l2rtspserver /dev/video0,/dev/video0  : one RTSP session with RTP audio and RTP video (ALSA device associatd with the V4L2 device)

Build
------- 
- Build  

		cmake . && make

	If live555 is not installed it will download it from live555.com and compile it. If asound is not installed, ALSA will be disabled.  
	If it still not work you will need to read Makefile.  

- Install (optional) 

		sudo make install

- Packaging  (optional)

		cpack .

Using Raspberry Pi Camera
------------------------- 
This RTSP server works with Raspberry Pi camera using :
- the opensource V4L2 driver bcm2835-v4l2

	sudo modprobe -v bcm2835-v4l2
	

Using v4l2loopback
----------------------- 
For camera providing uncompress format [v4l2tools](https://github.com/mpromonet/v4l2tools) can compress the video to an intermediate virtual V4L2 device [v4l2loopback](https://github.com/umlaeute/v4l2loopback):

	/dev/video0 (camera device)-> v4l2compress -> /dev/video10 (v4l2loopback device) -> v4l2rtspserver

This workflow could be set using :

	modprobe v4l2loopback video_nr=10
	v4l2compress -fH264 /dev/video0 /dev/video10 &
	v4l2rtspserver /dev/video10 &


Playing HTTP streams
-----------------------
When v4l2rtspserver is started with '-S' arguments it also give access to streams through HTTP.  
These streams could be played :

	* for MPEG-DASH with :   
           MP4Client http://..../unicast.mpd   
	   
	* for HLS with :  
           vlc http://..../unicast.m3u8  
           gstreamer-launch-1.0 playbin uri=http://.../unicast.m3u8  

It is now possible to play HLS url directly from browser :

 * using Firefox installing [Native HLS addons](https://addons.mozilla.org/en-US/firefox/addon/native_hls_playback)
 * using Chrome installing [Native HLS playback](https://chrome.google.com/webstore/detail/native-hls-playback/emnphkkblegpebimobpbekeedfgemhof)

There is also a small HTML page that use hls.js.

HLS & MPEG-DASH Streaming
-------------------------
When started with `-S` parameter, v4l2rtspserver provides HTTP streaming in HLS and MPEG-DASH formats.

### Basic HLS Setup

```bash
# Enable HLS with 5-second segments
./v4l2rtspserver -S5 /dev/video0

# Access HLS playlist
http://localhost:8554/ts.m3u8

# With custom port
./v4l2rtspserver -P 9999 -S5 /dev/video0
http://localhost:9999/ts.m3u8
```

### Playback Options

**Command Line Players:**
```bash
# VLC
vlc http://localhost:8554/ts.m3u8

# FFplay
ffplay http://localhost:8554/ts.m3u8

# GStreamer
gstreamer-launch-1.0 playbin uri=http://localhost:8554/ts.m3u8
```

**Browser Playback:**
- **Safari**: Native HLS support
- **Firefox**: Install [Native HLS addon](https://addons.mozilla.org/en-US/firefox/addon/native_hls_playback)
- **Chrome**: Install [Native HLS playback extension](https://chrome.google.com/webstore/detail/native-hls-playback/emnphkkblegpebimobpbekeedfgemhof)

### Troubleshooting Common Issues

**1. Format Compatibility**
```bash
# HLS requires H264 or H265 - force H264 if needed
./v4l2rtspserver -S5 -fH264 /dev/video0

# MJPEG/JPEG formats are NOT supported for HLS
```

**2. Safari/Browser Compatibility**
```bash
# Recommended settings for browser compatibility
./v4l2rtspserver -P 9999 -S5 -c -fH264 /dev/video0
```

**3. Segment Access Issues**  
If you see "405 Method Not Allowed" errors:
```bash
# Check playlist availability
curl -v "http://localhost:8554/ts.m3u8"

# Check specific segment (replace 0 with actual segment number)
curl -v "http://localhost:8554/ts?segment=0" --output test.ts
```

**4. Performance Optimization**
```bash
# Increase buffer for stability
./v4l2rtspserver -S5 -Q 10 -fH264 /dev/video0

# Optimal settings for reliability
./v4l2rtspserver -S3 -c -Q 15 -fH264 /dev/video0
```

### Key Parameters for HLS

- **`