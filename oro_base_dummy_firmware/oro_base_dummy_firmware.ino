#include "oro_protocol.h"
#include <math.h>

#define LED_BUILTIN 2
#define NAV_BUTTON_PIN 4

using namespace oro;

// ── Timing Variables ────────────────────────────────────────────────────────
unsigned long last_fast_update = 0;     // 100ms
unsigned long last_slow_update = 0;     // 1000ms
unsigned long last_env_update = 0;      // 5000ms
unsigned long last_display_refresh = 0; // 1000ms display refresh
unsigned long last_hb_update = 0;       // 1000ms heartbeat
unsigned long last_toggle_5s = 0;       // 5000ms state toggle
unsigned long last_toggle_8s = 0;       // 8000ms state toggle

// ── Button Debouncing ────────────────────────────────────────────────────────
bool last_button_reading = HIGH;
bool button_state = HIGH;
unsigned long last_debounce_time = 0;
const unsigned long debounce_delay = 50;

// ── Sequence Numbers ────────────────────────────────────────────────────────
uint8_t seq_nums[SID_COUNT] = {0};

// ── State Variables ─────────────────────────────────────────────────────────
float sim_time = 0.0f;
float camera_angle = 0.0f; // Range: -90.0 to 90.0
int32_t encoder_ticks = 0; // Range: -90 to 90
bool power_switch_state = true;
bool limit_switch_1_state = false;
bool limit_switch_2_state = false;
bool lid1_open = false;
bool lid2_open = false;
bool pump_state = false;
bool camera_motor_state = false;
int32_t display_value = 1234;
uint8_t nav_button_state = 1; // 1: Bowl 1, 2: Bowl 2, 3: Tank
uint8_t led_state = 0;

float sim_bowl1 = 0;
float sim_bowl2 = 0;
float sim_water_tank = 5.0f;
float sim_water_bowl = 0.5f;

// ── Forward Declarations ────────────────────────────────────────────────────
void sendAnalogPacket(SensorID id, float value, uint8_t priority = PRIO_LOW);
void sendDigitalPacket(SensorID id, uint8_t state, uint8_t priority = PRIO_LOW);
void sendEncoderPacket(SensorID id, int32_t ticks, uint8_t priority = PRIO_MED);
void sendPeripheralPacket(PeripheralID id, int32_t state,
                          uint8_t priority = PRIO_LOW);
void sendHeartbeat();
void processIncomingCommands();

// ── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ;
  }
  // All the sensor setup initializations happens here

  // Set builtin LED and Nav Button pin
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  pinMode(NAV_BUTTON_PIN, INPUT_PULLUP);
}

// ── Main Loop ───────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // 100ms: General simulation time update
  if (now - last_fast_update >= 100) {
    last_fast_update = now;
    sim_time += 0.1f;
  }

  // 1000ms: Heartbeat
  if (now - last_hb_update >= 1000) {
    last_hb_update = now;
    sendHeartbeat();
  }

  // 1000ms: Medium sensors (Weight, Water levels, Digital states)
  if (now - last_slow_update >= 1000) {
    last_slow_update = now;

    // Simulate Food Weight (sin wave 0 to 500g)
    sim_bowl1 = 250.0f + 250.0f * sin(sim_time * 0.5f);
    sim_bowl2 = 250.0f + 250.0f * cos(sim_time * 0.5f);
    sendAnalogPacket(SID_LOAD_LEFT, sim_bowl1);
    sendAnalogPacket(SID_LOAD_RIGHT, sim_bowl2);

    // Simulate Water levels
    // Tank is 5L, vary it slowly
    sim_water_tank = 5.0f - fmod(sim_time * 0.05f, 5.0f);
    // Bowl varies periodically
    sim_water_bowl = 0.5f + 0.5f * sin(sim_time * 0.2f);

    // Published as float (via fixed-point * 100), but value is floored before
    // send
    sendAnalogPacket(SID_WATER_LEVEL, (float)((int)sim_water_tank));
    sendAnalogPacket(SID_WATER_BOWL, sim_water_bowl);

    // Publish Encoder & Limit Switches
    sendEncoderPacket(SID_ENCODER, encoder_ticks);
    limit_switch_1_state = (encoder_ticks >= 90);
    limit_switch_2_state = (encoder_ticks <= -90);
    sendDigitalPacket(SID_LIMIT_SW1, limit_switch_1_state ? 1 : 0);
    sendDigitalPacket(SID_LIMIT_SW2, limit_switch_2_state ? 1 : 0);

    // Power switch state is always true
    power_switch_state = true;
    sendDigitalPacket(SID_POWER_SW, 1);

    // 2. Physical Nav Button Handling (GPIO 4 with Debounce)
    bool reading = digitalRead(NAV_BUTTON_PIN);

    // If the switch changed, due to noise or pressing:
    if (reading != last_button_reading) {
      last_debounce_time = now;
    }

    if ((now - last_debounce_time) > debounce_delay) {
      // If the button state has changed:
      if (reading != button_state) {
        button_state = reading;

        // If the new button state is LOW (Pressed):
        if (button_state == LOW) {
          // Cycle between 1, 2, 3
          nav_button_state = (nav_button_state % 3) + 1;

          // Publish the new Display State ID via Nav Button Topic (Priority:
          // HIGH)
          sendDigitalPacket(SID_NAV_BUTTON, nav_button_state, PRIO_HIGH);

          // Trigger an immediate display refresh with the selected sensor value
          float selected_val = 0;
          if (nav_button_state == 1)
            selected_val = sim_bowl1;
          else if (nav_button_state == 2)
            selected_val = sim_bowl2;
          else
            selected_val = (float)((int)sim_water_tank);

          sendPeripheralPacket(PID_DISPLAY, (int32_t)(selected_val * 100.0f));
        }
      }
    }
    last_button_reading = reading;

    // 3. Periodically refresh display with LATEST value of current selection
    if (now - last_display_refresh >= 1000) {
      last_display_refresh = now;

      float current_val = 0;
      if (nav_button_state == 1)
        current_val = sim_bowl1;
      else if (nav_button_state == 2)
        current_val = sim_bowl2;
      else
        current_val = (float)((int)sim_water_tank);

      sendPeripheralPacket(PID_DISPLAY, (int32_t)(current_val * 100.0f));

      // Also cycle LED indicator
      led_state = (led_state % 10) + 1;
      sendPeripheralPacket(PID_INDICATOR_LED, (int32_t)(led_state * 100));
    }

    // 5000ms: Environment
    if (now - last_env_update >= 5000) {
      last_env_update = now;

      // Battery slowly draining
      float battery = 100.0f - fmod(sim_time * 0.1f, 100.0f);
      sendAnalogPacket(SID_BATTERY, battery);

      // Temp & Humidity
      sendAnalogPacket(SID_TEMPERATURE, 22.5f + 2.0f * sin(sim_time * 0.1f));
      sendAnalogPacket(SID_HUMIDITY, 45.0f + 10.0f * cos(sim_time * 0.1f));
    }

    // State toggles
    if (now - last_toggle_5s >= 5000) {
      last_toggle_5s = now;
      power_switch_state = !power_switch_state;
      sendDigitalPacket(SID_POWER_SW, power_switch_state ? 1 : 0);

      lid1_open = !lid1_open;
      sendPeripheralPacket(PID_LID1_STEPPER, lid1_open ? 1 : 0);
      sendPeripheralPacket(PID_LID2_STEPPER, lid1_open ? 0 : 1);

      pump_state = !pump_state;
      sendPeripheralPacket(PID_PUMP, pump_state ? 1 : 0);

      if (((int)sim_time % 7) == 0) {
        limit_switch_1_state = !limit_switch_1_state;
        sendDigitalPacket(SID_LIMIT_SW1, limit_switch_1_state ? 1 : 0,
                          PRIO_HIGH);
      }

      if (((int)sim_time % 5) == 0) {
        limit_switch_2_state = !limit_switch_2_state;
        sendDigitalPacket(SID_LIMIT_SW2, limit_switch_2_state ? 1 : 0,
                          PRIO_HIGH);
      }
    }

    if (now - last_toggle_8s >= 8000) {
      last_toggle_8s = now;
      //   lid2_open = !lid2_open;
      //   sendPeripheralPacket(PID_LID2_STEPPER, lid2_open ? 1 : 0);

      camera_motor_state = !camera_motor_state;
      sendPeripheralPacket(PID_CAMERA_STEPPER, camera_motor_state ? 1 : 0);
    }
  }

  // Check for incoming commands from host
  processIncomingCommands();
}

// ── Packet Senders
// ──────────────────────────────────────────────────────────

// ── Sequence Numbers
//  One sequence array for Sensors, one for Peripherals
uint8_t seq_nums_sensor[SID_COUNT] = {0};
uint8_t seq_nums_periph[16] = {0};

void sendRawPacket(uint8_t priority, uint8_t type, uint8_t id,
                   int32_t payload) {
  OroPacket pkt;
  pkt.start = START_BYTE;
  pkt.msg_type = PACK_MSG_TYPE(priority, type);

  uint8_t seq = (type == MSG_PERIPHERAL_STATE) ? seq_nums_periph[id]
                                               : seq_nums_sensor[id];
  pkt.id_seq = PACK_ID_SEQ(seq, id);
  if (type == MSG_PERIPHERAL_STATE) {
    seq_nums_periph[id] = (seq + 1) & 0x0F;
  } else {
    seq_nums_sensor[id] = (seq + 1) & 0x0F;
  }

  pack_value_i32(pkt.value, payload);

  pkt.crc = oro_crc8(&pkt.msg_type, 6);

  Serial.write((uint8_t *)&pkt, PACKET_SIZE);
}

void sendAckPacket(uint8_t id, uint8_t seq, int32_t payload) {
  OroPacket pkt;
  pkt.start = START_BYTE;
  pkt.msg_type = PACK_MSG_TYPE(PRIO_HIGH, MSG_ACK);
  pkt.id_seq = PACK_ID_SEQ(seq, id);
  pack_value_i32(pkt.value, payload);
  pkt.crc = oro_crc8(&pkt.msg_type, 6);
  Serial.write((uint8_t *)&pkt, PACKET_SIZE);
}

void sendAnalogPacket(SensorID id, float value, uint8_t priority) {
  // Convert float to fixed-point int32_t (e.g., * 100)
  int32_t payload = (int32_t)(value * 100.0f);
  sendRawPacket(priority, MSG_SENSOR_DATA, (uint8_t)id, payload);
}

void sendDigitalPacket(SensorID id, uint8_t state, uint8_t priority) {
  // State is packed into LSB of value field by our protocol
  int32_t payload = (int32_t)state;
  sendRawPacket(priority, MSG_SENSOR_DATA, (uint8_t)id, payload);
}

void sendEncoderPacket(SensorID id, int32_t ticks, uint8_t priority) {
  sendRawPacket(priority, MSG_SENSOR_DATA, (uint8_t)id, ticks);
}

void sendPeripheralPacket(PeripheralID id, int32_t state, uint8_t priority) {
  sendRawPacket(priority, MSG_PERIPHERAL_STATE, (uint8_t)id, state);
}

void sendHeartbeat() {
  sendRawPacket(PRIO_CRIT, MSG_HEARTBEAT, (uint8_t)SID_HEARTBEAT, 1);
}

// ── Command Processor
// ───────────────────────────────────────────────────────

void processIncomingCommands() {
  static uint8_t rx_buffer[128];
  static size_t rx_head = 0;

  // Read all available bytes
  while (Serial.available()) {
    if (rx_head < sizeof(rx_buffer)) {
      rx_buffer[rx_head++] = Serial.read();
    } else {
      // Buffer overflow, drop oldest
      for (size_t i = 0; i < sizeof(rx_buffer) - 1; i++) {
        rx_buffer[i] = rx_buffer[i + 1];
      }
      rx_buffer[sizeof(rx_buffer) - 1] = Serial.read();
    }
  }

  // Try to frame packets
  while (rx_head >= PACKET_SIZE) {
    // Find start byte
    size_t start_idx = 0;
    while (start_idx < rx_head && rx_buffer[start_idx] != START_BYTE) {
      start_idx++;
    }

    // Shift buffer to start byte
    if (start_idx > 0) {
      rx_head -= start_idx;
      memmove(rx_buffer, rx_buffer + start_idx, rx_head);
    }

    if (rx_head < PACKET_SIZE)
      break;

    // Candidate packet found
    OroPacket *cand = (OroPacket *)rx_buffer;

    // Validate CRC
    if (validate_packet_crc(cand)) {
      // It's a valid packet!
      uint8_t type = GET_MSG_TYPE(cand->msg_type);
      SensorID sid = (SensorID)GET_ID(cand->id_seq);
      int32_t val = extract_value_i32(cand->value);

      if (type == MSG_COMMAND) {
        uint8_t id = GET_ID(cand->id_seq);
        uint8_t seq = GET_SEQ(cand->id_seq);

        if (id == PID_CAMERA_STEPPER) {
          // Set target angle within range [+90, -90]
          camera_angle = (float)val / 100.0f;
          if (camera_angle > 90.0f)
            camera_angle = 90.0f;
          if (camera_angle < -90.0f)
            camera_angle = -90.0f;

          // Encoder ticks = exactly the angle (1 tick per degree)
          encoder_ticks = (int32_t)camera_angle;

          // Publish current state feedback
          sendPeripheralPacket(PID_CAMERA_STEPPER,
                               (int32_t)(camera_angle * 100.0f));
        } else if (id == PID_PUMP) {
          pump_state = (val > 0);
          sendPeripheralPacket(PID_PUMP, pump_state ? 1 : 0);
        } else if (id == PID_LID1_STEPPER) {
          lid1_open = (val > 0);
          sendPeripheralPacket(PID_LID1_STEPPER, lid1_open ? 1 : 0);
        } else if (id == PID_LID2_STEPPER) {
          lid2_open = (val > 0);
          sendPeripheralPacket(PID_LID2_STEPPER, lid2_open ? 1 : 0);
        } else if (id == PID_DISPLAY) {
          display_value = val / 100; // Reciprocal of fixed-point packing
          sendPeripheralPacket(PID_DISPLAY, val);
        } else if (id == PID_INDICATOR_LED) {
          led_state = (uint8_t)(val / 100);
          sendPeripheralPacket(PID_INDICATOR_LED, val);
        }

        // Always ACK the command back with same SEQ and ID
        sendAckPacket(id, seq, val);
      }

      // Consume packet
      rx_head -= PACKET_SIZE;
      memmove(rx_buffer, rx_buffer + PACKET_SIZE, rx_head);
    } else {
      // CRC fail, skip this START_BYTE and try to re-frame
      rx_head -= 1;
      memmove(rx_buffer, rx_buffer + 1, rx_head);
    }
  }
}
