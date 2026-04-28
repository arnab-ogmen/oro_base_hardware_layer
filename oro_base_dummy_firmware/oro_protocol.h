#ifndef ORO_PROTOCOL_H
#define ORO_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

namespace oro {

// ── Wire Constants ──────────────────────────────────────────────────────────
static const uint8_t START_BYTE = 0xAA;
static const size_t PACKET_SIZE = 8;

// ── Message Types ──────────────────────────────────────────────────────────
static const uint8_t MSG_SENSOR_DATA = 0x01;
static const uint8_t MSG_PERIPHERAL_STATE = 0x02;
static const uint8_t MSG_HEARTBEAT = 0x03;
static const uint8_t MSG_COMMAND = 0x04;
static const uint8_t MSG_ACK = 0x05;

// ── Priority Levels ────────────────────────────────────────────────────────
static const uint8_t PRIO_LOW = 0x00;  // 0b00
static const uint8_t PRIO_MED = 0x01;  // 0b01
static const uint8_t PRIO_HIGH = 0x02; // 0b10
static const uint8_t PRIO_CRIT = 0x03; // 0b11

inline uint8_t GET_MSG_TYPE(uint8_t b) { return b & 0x3F; }
inline uint8_t GET_PRIORITY(uint8_t b) { return (b >> 6) & 0x03; }
inline uint8_t PACK_MSG_TYPE(uint8_t prio, uint8_t type) {
  return (uint8_t)(((prio & 0x03) << 6) | (type & 0x3F));
}

inline uint8_t GET_ID(uint8_t b) { return b & 0x0F; }
inline uint8_t GET_SEQ(uint8_t b) { return (b >> 4) & 0x0F; }
inline uint8_t PACK_ID_SEQ(uint8_t seq, uint8_t id) {
  return (uint8_t)(((seq & 0x0F) << 4) | (id & 0x0F));
}

enum SensorID {
  SID_LOAD_LEFT = 0x00,
  SID_LOAD_RIGHT = 0x01,
  SID_WATER_LEVEL = 0x02,
  SID_WATER_BOWL = 0x03,
  SID_HUMIDITY = 0x04,
  SID_TEMPERATURE = 0x05,
  SID_LIMIT_SW1 = 0x06,
  SID_LIMIT_SW2 = 0x07,
  SID_ENCODER = 0x08,
  SID_HOME_SENSOR = 0x09,
  SID_POWER_SW = 0x0A,
  SID_BATTERY = 0x0B,
  SID_HEARTBEAT = 0x0C,
  SID_NAV_BUTTON = 0x0D,
  SID_LID1_HALL = 0x0E,
  SID_LID2_HALL = 0x0F,
  SID_COUNT = 16
};

enum PeripheralID {
  PID_PUMP = 0x00,
  PID_LID1_STEPPER = 0x01,
  PID_LID2_STEPPER = 0x02,
  PID_CAMERA_STEPPER = 0x03,
  PID_DISPLAY = 0x04,
  PID_INDICATOR_LED = 0x05,
  PID_CAMERA_SERVO = 0x06,
};

static const int32_t ACK_SUCCESS = 0;
static const int32_t ACK_ERROR = 1;
static const int32_t ACK_TIMEOUT = 2;
static const int32_t ACK_BUSY = 3;
static const int32_t ACK_INVALID = 4;
// ── OroPacket: 8-byte fixed-size wire format
// ────────────────────────────────
struct OroPacket {
  uint8_t start;    // Byte 0: Start-of-frame marker (0xAA)
  uint8_t msg_type; // Byte 1: [7:6]=priority, [5:0]=type
  uint8_t id_seq;   // Byte 2: [7:4]=seq_num,  [3:0]=sensor_id
  uint8_t value[4]; // Bytes 3-6: int32_t big-endian payload
  uint8_t crc;      // Byte 7: CRC-8 over bytes[1..6]
} __attribute__((packed));

static_assert(sizeof(struct OroPacket) == 8,
              "OroPacket must be exactly 8 bytes");

// ── CRC-8 ───────────────────────────────────────────────────────────────────
inline uint8_t oro_crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x80) {
        crc = (uint8_t)((crc << 1) ^ 0x07);
      } else {
        crc = (uint8_t)(crc << 1);
      }
    }
  }
  return crc;
}

// ── Payload value helpers ───────────────────────────────────────────────────
inline int32_t extract_value_i32(const uint8_t value[4]) {
  return (int32_t)(((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16) |
                   ((uint32_t)value[2] << 8) | ((uint32_t)value[3]));
}

inline float fixed_to_float(const uint8_t value[4]) {
  return (float)(extract_value_i32(value)) / 100.0f;
}

inline void pack_value_i32(uint8_t value[4], int32_t v) {
  uint32_t u = (uint32_t)v;
  value[0] = (uint8_t)((u >> 24) & 0xFF);
  value[1] = (uint8_t)((u >> 16) & 0xFF);
  value[2] = (uint8_t)((u >> 8) & 0xFF);
  value[3] = (uint8_t)((u)&0xFF);
}

inline bool validate_packet_crc(const struct OroPacket *pkt) {
  uint8_t computed = oro_crc8(&pkt->msg_type, 6);
  return computed == pkt->crc;
}

} // namespace oro

#endif // ORO_PROTOCOL_H
