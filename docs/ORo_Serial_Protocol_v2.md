# ORo Serial Protocol V2: Message Specification

This document provides a detailed technical breakdown of the communication between the ORo Host (Middleware) and the Firmware (MCU).

## 1. OroPacket: Binary Wire Format

All communication over UART uses a fixed-size 8-byte packet structure.

| Byte | Field | Description |
| :--- | :--- | :--- |
| 0 | `start` | Start-of-frame marker (**0xAA**) |
| 1 | `msg_type` | [7:6] Priority, [5:0] Message Type |
| 2 | `id_seq` | [7:4] Sequence Number (0-15), [3:0] ID (Sensor/Peripheral) |
| 3-6 | `value` | `int32_t` Big-Endian Payload |
| 7 | `crc` | CRC-8 (Polynomial 0x07) over bytes 1-6 |

### Message Types
- `0x01`: **MSG_SENSOR_DATA** (MCU -> Host)
- `0x02`: **MSG_PERIPHERAL_STATE** (MCU -> Host)
- `0x03`: **MSG_HEARTBEAT** (MCU -> Host)
- `0x04`: **MSG_COMMAND** (Host -> MCU)
- `0x05`: **MSG_ACK** (MCU -> Host)

---

## 2. Communication Flows

### 2.1 Telemetry Flow (MCU → Host)
1. **Source**: Physical sensor readout on MCU.
2. **Packing**: Value is converted to fixed-point (usually `val * 100`) and packed into an `OroPacket`.
3. **Framing**: Packet is sent via UART.
4. **Middleware**: `McuSerialReaderNode` validates CRC, extracts the value, and publishes it to ZMQ under the corresponding topic.

### 2.2 Command Flow (Host → MCU)
1. **Source**: Dashboard (ZMQ) or Host Service.
2. **Middleware**: `CommandIngressNode` receives ZMQ JSON, looks up the `PeripheralID` in the registry.
3. **Serialization**: Numeric value is packed into an `OroPacket` (fixed-point `* 100` for angles/analog, raw `int` for binary).
4. **Dispatch**: Sent via REQ/REP to `McuSerialReaderNode`, which writes it to UART.
5. **FW Processing**: MCU parses the packet, executes the action, and immediately returns a `MSG_ACK` with the same sequence number.
6. **Validation**: `McuSerialReaderNode` waits for the matching ACK and returns the status to the caller.

---

## 3. Topic Registry Reference (46 Topics)

### 3.1 Sensor Data (/sensors/...)
*Source: UART (MCU Sensors)*

| TID | ZMQ Topic | Category | ID (Hex) | Packing/Scale |
| :--- | :--- | :--- | :--- | :--- |
| 0 | `/sensors/food_weight/bowl_1` | ANALOG | 0x00 | `grams * 100` |
| 1 | `/sensors/food_weight/bowl_2` | ANALOG | 0x01 | `grams * 100` |
| 2 | `/sensors/water_level/tank` | ANALOG | 0x02 | `liters * 100` |
| 3 | `/sensors/water_level/bowl` | DIGITAL | 0x03 | 0: Full, 1: Not Full |
| 4 | `/sensors/environment/humidity` | ANALOG | 0x04 | `%RH * 100` |
| 5 | `/sensors/environment/temperature` | ANALOG | 0x05 | `°C * 100` |
| 6 | `/sensors/camera_rotation/limit_switch_1` | DIGITAL | 0x06 | 0: Open, 1: Closed |
| 7 | `/sensors/camera_rotation/limit_switch_2` | DIGITAL | 0x07 | 0: Open, 1: Closed |
| 8 | `/sensors/camera_rotation/optical_encoder` | ENCODER | 0x08 | Raw `int32` ticks |
| 9 | `/sensors/camera_rotation/home` | DIGITAL | 0x09 | 0: Not Home, 1: Home |
| 31 | `/sensors/treat/level_indicator_ir` | DIGITAL | -1 (Host) | Internal logic |
| 32 | `/sensors/treat/sorter_ir` | DIGITAL | -1 (Host) | Internal logic |
| 33 | `/sensors/treat/thrower_ir` | DIGITAL | -1 (Host) | Internal logic |
| 34 | `/sensors/thermal/ir_array` | THERMAL | -1 (Host) | 8x8 matrix frame |
| 35 | `/sensors/nav_button` | ANALOG | 0x0D | `state * 100` (1.0, 2.0, 3.0) |

### 3.2 System Topics (/system/...)
*Source: Mixed (MCU & Host)*

| TID | ZMQ Topic | Source | ID | Description |
| :--- | :--- | :--- | :--- | :--- |
| 10 | `/system/power/switch` | UART | 0x0A | Power switch state |
| 11 | `/system/power/battery_level` | UART | 0x0B | `% * 100` |
| 12 | `/system/time/clock` | SYSTEM | -1 | Unix Timestamp |
| 13 | `/system/device/heartbeat` | UART | 0x0C | Rolling sequence |
| 14 | `/system/connectivity/state` | SYSTEM | -1 | 0: None, 1: Local, 2: Internet |
| 44 | `/system/reserved/overbound` | SYSTEM | -1 | Diagnostic error topic |

### 3.3 Status Topics (/status/...)
*Source: UART (MCU Actuator Feedback)*

| TID | ZMQ Topic | Category | ID | Mapping |
| :--- | :--- | :--- | :--- | :--- |
| 15 | `/status/lid/1` | ANALOG | 0x0E | 0.0: Closed, 1.0: Open, 3.0: Trans |
| 16 | `/status/lid/2` | ANALOG | 0x0F | 0.0: Closed, 1.0: Open, 3.0: Trans |
| 17 | `/status/water_pump` | DIGITAL | 0x00 | 0: Idle, 1: Pumping |
| 18 | `/status/camera_rotation/stepper_motor` | DIGITAL | 0x03 | 0: Idle, 1: Running |
| 19 | `/status/display/seven_segment` | ANALOG | 0x04 | Current displayed value * 100 |
| 20 | `/status/led_indicator` | ANALOG | 0x05 | LED code * 100 |
| 42 | `/status/lid_motor/1` | DIGITAL | 0x01 | 0: Idle, 1: Running |
| 43 | `/status/lid_motor/2` | DIGITAL | 0x02 | 0: Idle, 1: Running |

### 3.4 Command Topics (/commands/...)
*Source: Host → MCU via MSG_COMMAND (Except Host-Only Services)*

| TID | ZMQ Topic | Target | PID | Packing |
| :--- | :--- | :--- | :--- | :--- |
| 21 | `/commands/camera_rotation` | MCU | 0x03 | `angle * 100` |
| 36 | `/commands/pump` | MCU | 0x00 | 0: OFF, 1: ON |
| 37 | `/commands/lid/1` | MCU | 0x01 | 0: CLOSE, 1: OPEN |
| 38 | `/commands/lid/2` | MCU | 0x02 | 0: CLOSE, 1: OPEN |
| 39 | `/commands/display` | MCU | 0x04 | `value * 100` |
| 40 | `/commands/led` | MCU | 0x05 | `code * 100` |
| 41 | `/commands/camera_rotation_servo` | MCU | 0x06 | 0: Disengage, 1: Engage |
| 22 | `/commands/feed` | HOST | -1 | Radxa Service (JSON) |
| 23 | `/commands/treat/dispense` | HOST | -1 | Radxa Service (JSON) |
| 24 | `/commands/photo_capture` | HOST | -1 | Radxa Service (JSON) |
| 25 | `/commands/live_session/start` | HOST | -1 | Radxa Service (JSON) |
| 26 | `/commands/live_session/end` | HOST | -1 | Radxa Service (JSON) |
| 27 | `/commands/camera/ir_control` | HOST | -1 | Radxa Service (JSON) |
| 28 | `/commands/audio/speakers` | HOST | -1 | Radxa Service (JSON) |
| 29 | `/commands/settings/apply` | HOST | -1 | Radxa Service (JSON) |
| 30 | `/commands/firmware/update` | HOST | -1 | Radxa Service (JSON) |

---

## 4. Response & Validation

Upon receiving a `MSG_COMMAND`, the FW issues a `MSG_ACK` packet.

### ACK Status Codes
- `0`: **ACK_SUCCESS** - Command received and action initiated/completed.
- `1`: **ACK_ERROR** - Internal hardware error.
- `2`: **ACK_TIMEOUT** - Internal wait timeout exceeded.
- `3`: **ACK_BUSY** - Actuator is already performing another action.
- `4`: **ACK_INVALID** - Command ID or value is out of bounds.

The Middleware uses the **Sequence Number** to match the ACK to the original request, allowing for multiple overlapping commands to different actuators.
