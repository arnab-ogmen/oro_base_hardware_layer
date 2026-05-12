#!/bin/bash
echo "Starting Live Feed and Audio Playback (1080p MJPEG)..."
echo "Press Ctrl+C to stop."

# --- Pre-flight Checks & Fixes ---
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
    echo "CRITICAL ERROR: /dev/video0 not found! Please check the camera connection."
    exit 1
fi

# Ensure permissions for Allwinner Video Engine (Cedar)
if [ -e /dev/cedar_dev ]; then
    sudo chmod 666 /dev/cedar_dev /dev/cedar_dev_ve2 2>/dev/null || echo "Note: Permission check failed. Run with sudo if video fails."
fi

# We run a single combined pipeline to keep audio and video synchronized by the GStreamer clock.
# Video comes from v4l2-ctl via pipe to bypass broken v4l2src element on this board.
# Audio comes from USB PnP Audio Device mic and plays through its speaker.

v4l2-ctl -d /dev/video0 --set-fmt-video=width=1280,height=720,pixelformat=MJPG --stream-mmap --stream-to=- | \
/usr/bin/gst-launch-1.0 \
    fdsrc do-timestamp=true ! image/jpeg,width=1280,height=720,framerate=30/1 ! jpegparse ! jpegdec ! videoconvert ! queue max-size-buffers=5 leaky=downstream ! autovideosink sync=false \
    alsasrc device=plughw:$AUDIO_CARD,0 ! audioconvert ! audioresample ! audio/x-raw,rate=16000,channels=1,format=S16LE ! queue ! alsasink device=plughw:$AUDIO_CARD,0 sync=true
