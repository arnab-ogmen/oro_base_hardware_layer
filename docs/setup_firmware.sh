#!/bin/bash
# ORo Base Firmware Dependency Installer
# This script automates the installation of the ESP32 core and all 
# required libraries for the ORo Base Test Firmware.

set -e

echo "-------------------------------------------------------"
echo "  ORo Base Firmware: Automated Environment Setup"
echo "-------------------------------------------------------"

# 1. Update Index and Install Core
echo "[1/2] Updating ESP32 core index..."
arduino-cli core update-index

echo "[1/2] Installing ESP32 core (esp32:esp32)..."
arduino-cli core install esp32:esp32

# 2. Install Libraries
echo "[2/2] Installing required libraries..."

# Array of libraries with specific versions
LIBS=(
  "Adafruit AHTX0@2.0.6"
  "Adafruit BusIO@1.17.4"
  "Adafruit Unified Sensor@1.1.15"
  "HX711@0.6.3"
  "Stepper@1.1.3"
  "ESP32Servo@3.2.0"
  "SparkFun Qwiic Alphanumeric Display Arduino Library@2.2.11"
  "TCA9555@0.4.4"
)

for lib in "${LIBS[@]}"; do
  echo "  --> Installing $lib..."
  arduino-cli lib install "$lib"
done

echo "-------------------------------------------------------"
echo "Setup Complete! You can now compile the firmware using:"
echo "arduino-cli compile --fqbn esp32:esp32:esp32s3 ."
echo "-------------------------------------------------------"
