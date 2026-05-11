#!/bin/bash
# Phase 1: Hardware Bring-Up tests for Radxa A7z

echo "=================================================="
echo "Phase 1: Hardware Bring-Up Tests"
echo "=================================================="

echo "[1] Checking V4L2 Video Devices..."
v4l2-ctl --list-devices
echo ""

echo "[2] Checking ALSA Audio Devices..."
arecord -l
echo ""

echo "[3] Checking Video Capabilities on /dev/video2..."
v4l2-ctl -d /dev/video2 --list-formats-ext || echo "/dev/video2 not found or permission denied"
echo ""

echo "[4] Testing Video Pipeline with fakesink..."
echo "Running: /usr/bin/gst-launch-1.0 v4l2src device=/dev/video2 num-buffers=30 ! image/jpeg,width=3840,height=2160,framerate=30/1 ! fakesink"
/usr/bin/gst-launch-1.0 v4l2src device=/dev/video2 num-buffers=30 ! image/jpeg,width=3840,height=2160,framerate=30/1 ! fakesink || echo "Video pipeline test failed. Ensure camera is connected to /dev/video2"
echo ""

echo "[5] Testing Audio Pipeline with fakesink..."
echo "Running: /usr/bin/gst-launch-1.0 alsasrc device=hw:2,0 num-buffers=100 ! audioconvert ! audioresample ! audio/x-raw,rate=16000,channels=1,format=S16LE ! fakesink"
/usr/bin/gst-launch-1.0 alsasrc device=hw:2,0 num-buffers=100 ! audioconvert ! audioresample ! audio/x-raw,rate=16000,channels=1,format=S16LE ! fakesink || echo "Audio pipeline test failed. Ensure mic is hw:2,0"
echo ""

echo "Phase 1 Testing complete."
