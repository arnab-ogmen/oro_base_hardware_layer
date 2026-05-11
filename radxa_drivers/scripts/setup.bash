#!/bin/bash
set -e

echo "Installing Radxa A7z Media Middleware Dependencies..."

sudo apt-get update

# Core Build Tools & Utilities
sudo apt-get install -y build-essential cmake pkg-config v4l-utils

# GStreamer dependencies
sudo apt-get install -y \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-tools \
    gstreamer1.0-alsa

# ZeroMQ dependencies
sudo apt-get install -y libzmq3-dev

# FlatBuffers compiler and headers
sudo apt-get install -y flatbuffers-compiler libflatbuffers-dev

# Spdlog and Json dependency
sudo apt-get install -y libspdlog-dev nlohmann-json3-dev

echo "Dependencies installed successfully!"
