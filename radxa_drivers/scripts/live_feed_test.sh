#!/bin/bash
echo "Starting Live Feed and Audio Playback..."
echo "Press Ctrl+C to stop."

# We run a single combined pipeline to keep audio and video synchronized by the GStreamer clock.
# Video goes to autovideosink (opens a window on your desktop)
# Audio goes to autoaudiosink (plays through your default speakers)

# Option 1: MJPG (Compressed, allows higher resolution but uses more CPU for decoding)
# /usr/bin/gst-launch-1.0 \
#     v4l2src device=/dev/video2 ! image/jpeg,width=2560,height=1440,framerate=30/1 ! jpegparse ! jpegdec ! videoconvert ! queue max-size-buffers=2 ! autovideosink sync=true \
#     alsasrc device=hw:2,0 ! audioconvert ! audioresample ! queue ! autoaudiosink sync=true

# Option 2: YUYV (Uncompressed, low CPU usage but restricted to lower resolutions/bandwidth)
/usr/bin/gst-launch-1.0 \
    v4l2src device=/dev/video2 ! video/x-raw,format=YUY2,width=640,height=480,framerate=30/1 ! videoconvert ! queue max-size-buffers=2 ! autovideosink sync=true \
    alsasrc device=hw:2,0 ! audioconvert ! audioresample ! queue ! autoaudiosink sync=true
