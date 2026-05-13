#!/bin/bash
set -euo pipefail

usage() {
    cat <<EOF
Usage: $0 <video_port> [width height framerate]

Example:
  $0 2 1280 720 30
  $0 /dev/video4 640 480 15

This script verifies a virtual camera device created by the camsplitter node and
launches a live preview using GStreamer.
EOF
}

if [[ $# -lt 1 ]]; then
    usage
    exit 1
fi

VIDEO_PORT="$1"
WIDTH="1280"
HEIGHT="720"
FRAMERATE="30"

if [[ $# -ge 4 ]]; then
    WIDTH="$2"
    HEIGHT="$3"
    FRAMERATE="$4"
fi

if [[ "$VIDEO_PORT" =~ ^[0-9]+$ ]]; then
    DEVICE="/dev/video${VIDEO_PORT}"
else
    DEVICE="$VIDEO_PORT"
fi

if [[ ! -e "$DEVICE" ]]; then
    echo "ERROR: Device '$DEVICE' does not exist."
    exit 1
fi

if ! command -v gst-launch-1.0 >/dev/null 2>&1; then
    echo "ERROR: gst-launch-1.0 is required but not installed."
    exit 1
fi

cat <<EOF
Checking virtual camera feed on: $DEVICE
Target capture: ${WIDTH}x${HEIGHT}@${FRAMERATE}
EOF

if ! command -v v4l2-ctl >/dev/null 2>&1; then
    echo "\nWARNING: v4l2-ctl not installed; device capability check skipped."
else
    printf "\nDevice info:\n"
    v4l2-ctl -d "$DEVICE" --all || true
fi

device_caps=$(v4l2-ctl -d "$DEVICE" --all 2>/dev/null | tr -d '\r')
if echo "$device_caps" | grep -q "Video Capture"; then
    echo "\nStarting live preview through v4l2-ctl → fdsrc. Press Ctrl+C to stop."

    v4l2-ctl -d "$DEVICE" --set-fmt-video=width=${WIDTH},height=${HEIGHT},pixelformat=MJPG --stream-mmap --stream-to=- | \
        /usr/bin/gst-launch-1.0 \
            fdsrc do-timestamp=true ! \
            image/jpeg,width=${WIDTH},height=${HEIGHT},framerate=${FRAMERATE}/1 ! \
            jpegparse ! jpegdec ! videoconvert ! \
            queue max-size-buffers=5 leaky=downstream ! \
            autovideosink sync=false
elif echo "$device_caps" | grep -q "Video Output"; then
    echo "\nERROR: Device '$DEVICE' is a v4l2loopback output-only device."
    echo "It is a writer endpoint, not a readable camera source."
    echo "To validate the feed, inspect the actual capture source or the process writing into this virtual camera."
    echo "For example, test /dev/video0 with the known working command:"
    echo "  v4l2-ctl -d /dev/video0 --set-fmt-video=width=${WIDTH},height=${HEIGHT},pixelformat=MJPG --stream-mmap --stream-to=- | \""
    echo "      /usr/bin/gst-launch-1.0 fdsrc do-timestamp=true ! image/jpeg,width=${WIDTH},height=${HEIGHT},framerate=${FRAMERATE}/1 ! jpegparse ! jpegdec ! videoconvert ! queue max-size-buffers=2 leaky=downstream ! autovideosink sync=false"
    exit 1
else
    echo "\nERROR: Device '$DEVICE' does not expose a readable capture interface."
    exit 1
fi
