#!/bin/bash
# Phase 1: Hardware Bring-Up tests for Radxa A7z

echo "=================================================="
echo "Phase 1: Hardware Bring-Up Tests"
echo "=================================================="

# --- Pre-flight Checks & Fixes ---

echo "[0] Applying Hardware Fixes..."
# Find USB PnP Audio Device card number
AUDIO_CARD=$(aplay -l | grep -i "USB PnP Audio Device" | head -n 1 | cut -d' ' -f2 | tr -d ':')
if [ -z "$AUDIO_CARD" ]; then
    echo "Error: USB PnP Audio Device not found!"
    # fallback to card 2 if search fails but card 2 exists
    if aplay -l | grep -q "card 2"; then AUDIO_CARD=2; else exit 1; fi
fi
echo "Using Audio Card: $AUDIO_CARD"

# Check for camera existence
if [ ! -e /dev/video0 ]; then
    echo "CRITICAL ERROR: /dev/video0 not found! Is the 4K camera plugged in?"
    # Don't exit yet, let other tests run if possible, but video will fail
fi

# Check for UVC Quirk 0x80 (Needed for USB 2.0 bandwidth)
QUIRK=$(cat /sys/module/uvcvideo/parameters/quirks 2>/dev/null || echo "0")
if [ "$QUIRK" != "128" ]; then
    echo "Warning: uvcvideo quirk 0x80 not detected. High resolution might fail over USB 2.0."
    echo "Run: 'sudo modprobe -r uvcvideo && sudo modprobe uvcvideo quirks=0x80' if video fails."
fi

# Ensure permissions for Allwinner Video Engine (Cedar)
if [ -e /dev/cedar_dev ]; then
    sudo chmod 666 /dev/cedar_dev /dev/cedar_dev_ve2 2>/dev/null || echo "Note: Could not chmod /dev/cedar_dev (run as sudo if GStreamer fails)"
fi
echo ""

echo "[1] Checking V4L2 Video Devices..."
v4l2-ctl --list-devices
echo ""

echo "[2] Checking ALSA Audio Devices..."
arecord -l
echo ""

echo "[3] Checking Video Capabilities on /dev/video0..."
if [ -e /dev/video0 ]; then
    v4l2-ctl -d /dev/video0 --list-formats-ext
else
    echo "SKIPPING: /dev/video0 not found."
fi
echo ""

echo "[4] Testing Video Pipeline with fakesink..."
# Note: Using v4l2-ctl pipe workaround because v4l2src is incompatible with USB UVC cameras on this board
if [ -e /dev/video0 ]; then
    echo "Running: v4l2-ctl -d /dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=MJPG --stream-mmap --stream-to=- | gst-launch-1.0 fdsrc num-buffers=30 ! image/jpeg,width=1920,height=1080 ! jpegparse ! jpegdec ! videoconvert ! fakesink"
    v4l2-ctl -d /dev/video0 --set-fmt-video=width=1920,height=1080,pixelformat=MJPG --stream-mmap --stream-count=30 --stream-to=- | \
    /usr/bin/gst-launch-1.0 fdsrc ! \
    image/jpeg,width=1920,height=1080,framerate=30/1 ! \
    jpegparse ! jpegdec ! videoconvert ! fakesink || echo "Video pipeline test failed."
else
    echo "SKIPPING: Video pipeline test (Camera missing)."
fi
echo ""

echo "[5] Testing Audio Pipeline with USB PnP Audio Device..."
# Card dynamic = USB PnP Audio Device (mic + speaker)
echo "Running: /usr/bin/gst-launch-1.0 alsasrc device=plughw:$AUDIO_CARD,0 num-buffers=100 ! audioconvert ! audioresample ! audio/x-raw,rate=16000,channels=1,format=S16LE ! alsasink device=plughw:$AUDIO_CARD,0"
/usr/bin/gst-launch-1.0 alsasrc device=plughw:$AUDIO_CARD,0 num-buffers=100 ! audioconvert ! audioresample ! audio/x-raw,rate=16000,channels=1,format=S16LE ! alsasink device=plughw:$AUDIO_CARD,0 || echo "Audio pipeline test failed."
echo ""

echo "Phase 1 Testing complete."
