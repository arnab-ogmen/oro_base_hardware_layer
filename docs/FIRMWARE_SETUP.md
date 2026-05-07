# ORo Base Firmware Setup Guide

This document outlines the dependencies and installation steps for the ORo Base Test Firmware.

## System Dependencies
- **arduino-cli**: The primary tool for compiling and uploading firmware from the command line.

## Target Hardware
- **Core**: ESP32
- **Board**: ESP32S3 Dev Module (`esp32:esp32:esp32s3`)

## Library Dependencies
The following libraries must be installed in the Arduino environment:

| Library Name | Version | Purpose |
|--------------|---------|---------|
| `Adafruit AHTX0` | 2.0.6 | Ambient Temperature & Humidity |
| `Adafruit BusIO` | 1.17.4 | Required by Adafruit libraries |
| `Adafruit Unified Sensor` | 1.1.15 | Sensor abstraction layer |
| `HX711` | 0.6.3 | Load cells (Scale) interfacing |
| `Stepper` | 1.1.3 | Stepper motor control (built-in) |
| `ESP32Servo` | 3.2.0 | Servo motor control for ESP32 |
| `SparkFun Qwiic Alphanumeric Display Arduino Library` | 2.2.11 | 14-segment display control |
| `TCA9555` | 0.4.4 | I2C GPIO Expansion (Lids, Homing) |

## Automated Installation
A single-shot bash script is provided in the repository to automate the environment setup:
`./setup_firmware.sh`

## Manual Compilation
Once dependencies are installed, you can compile the firmware using:
```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3 oro_base/oro_base_hardware_layer/oro_base_test_firmware/oro_base_test_firmware.ino
```
