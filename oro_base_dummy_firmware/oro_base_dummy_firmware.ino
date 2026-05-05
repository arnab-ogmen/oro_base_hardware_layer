#include "oro_protocol.h"
#include <math.h>

#define LED_BUILTIN 2
#define NAV_BUTTON_PIN 4

using namespace oro;

// ── Task Handlers ───────────────────────────────────────────────────────────
TaskHandle_t HeartbeatTaskHandle = NULL;
TaskHandle_t SensorsTaskHandle = NULL;
TaskHandle_t NavButtonTaskHandle = NULL;
TaskHandle_t DisplayTaskHandle = NULL;
TaskHandle_t EnvTaskHandle = NULL;
TaskHandle_t ToggleTaskHandle = NULL;
TaskHandle_t CameraTaskHandle = NULL;
TaskHandle_t CommandTaskHandle = NULL;
TaskHandle_t LidHallTaskHandle = NULL;

// ── Sequence Numbers ────────────────────────────────────────────────────────
uint8_t seq_nums_sensor[SID_COUNT] = {0};
uint8_t seq_nums_periph[16] = {0};

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
bool lid1_motor_running = false;
bool lid2_motor_running = false;
bool camera_motor_running = false;
bool camera_servo_running = false;
int32_t display_value = 1234;
uint8_t nav_button_state = 1; // 1: Bowl 1, 2: Bowl 2, 3: Tank
uint8_t led_state = 0;
float camera_servo_angle = 0.0f;
bool camera_servo_engaged = false;

// Lid physical state (Hall sensor): 0=closed, 1=open, 3=transition
uint8_t lid1_hall_state = 0;
uint8_t lid2_hall_state = 0;

float sim_bowl1 = 0.0f;
float sim_bowl2 = 0.0f;
float sim_water_tank = 5.0f;
float sim_water_bowl = 0.5f;

// ── Async Action Tracking ──────────────────────────────────────────────────
struct ActiveAction {
  uint8_t id;
  uint8_t seq;
  uint32_t start_ms;
  uint32_t duration_ms;
  int32_t final_val;
  bool active;
};
ActiveAction active_actions[8] = {0};

// ── Forward Declarations ────────────────────────────────────────────────────
void sendAnalogPacket(SensorID id, float value, uint8_t priority = PRIO_LOW);
void sendDigitalPacket(SensorID id, uint8_t state, uint8_t priority = PRIO_LOW);
void sendEncoderPacket(SensorID id, int32_t ticks, uint8_t priority = PRIO_MED);
void sendPeripheralPacket(PeripheralID id, int32_t state,
                          uint8_t priority = PRIO_LOW);
void sendHeartbeat();
void processIncomingCommands();
void checkActiveActions();
void registerAsyncAction(uint8_t id, uint8_t seq, uint32_t duration_ms,
                         int32_t val);

// ── Tasks ───────────────────────────────────────────────────────────────────

void HeartbeatTask(void *pvParameters) {
  for (;;) {
    sendHeartbeat();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void SensorsTask(void *pvParameters) {
  for (;;) {
    sim_time += 1.0f;

    // Simulate Food Weight (sin wave 0 to 500g)
    sim_bowl1 = 250.0f + 250.0f * sin(sim_time * 0.05f);
    sim_bowl2 = 250.0f + 250.0f * cos(sim_time * 0.05f);
    sendAnalogPacket(SID_LOAD_LEFT, sim_bowl1);
    sendAnalogPacket(SID_LOAD_RIGHT, sim_bowl2);

    // Simulate Water levels
    sim_water_tank = 5.0f - fmod(sim_time * 0.005f, 5.0f);
    sim_water_bowl = (fmod(sim_time, 3.0f) == 0.0f) ? 1.0f : 0.0f;
    sendAnalogPacket(SID_WATER_LEVEL, (float)((int)sim_water_tank));
    sendDigitalPacket(SID_WATER_BOWL, (uint8_t)sim_water_bowl);

    // Publish Encoder & Limit Switches
    sendEncoderPacket(SID_ENCODER, encoder_ticks);
    limit_switch_1_state = (encoder_ticks >= 90);
    limit_switch_2_state = (encoder_ticks <= -90);
    sendDigitalPacket(SID_LIMIT_SW1, limit_switch_1_state ? 1 : 0);
    sendDigitalPacket(SID_LIMIT_SW2, limit_switch_2_state ? 1 : 0);

    // Power switch state is always true
    sendDigitalPacket(SID_POWER_SW, 1);

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void NavButtonTask(void *pvParameters) {
  for (;;) {
    // ── Simulated nav button press (auto-cycles every 3s) ─────────────────
    // Actual hardware code commented out below:
    //
    // bool last_button_reading = HIGH;
    // bool button_state = HIGH;
    // unsigned long last_debounce_time = 0;
    // const unsigned long debounce_delay = 50;
    // bool reading = digitalRead(NAV_BUTTON_PIN);
    // unsigned long now = millis();
    // if (reading != last_button_reading) last_debounce_time = now;
    // if ((now - last_debounce_time) > debounce_delay) {
    //   if (reading != button_state) {
    //     button_state = reading;
    //     if (button_state == LOW) { /* button pressed logic */ }
    //   }
    // }
    // last_button_reading = reading;

    // Cycle: 1 = Food bowl 1, 2 = Food bowl 2, 3 = Water tank level
    nav_button_state = (nav_button_state % 3) + 1;

    // Publish nav button state as ANALOG (fixed-point ×100)
    // Subscriber decodes: 1.0=Bowl1, 2.0=Bowl2, 3.0=Tank
    sendAnalogPacket(SID_NAV_BUTTON, (float)nav_button_state);

    // Update display and LED indicator to match selection
    float selected_val = 0;
    if (nav_button_state == 1)
      selected_val = sim_bowl1;
    else if (nav_button_state == 2)
      selected_val = sim_bowl2;
    else
      selected_val = (float)((int)sim_water_tank);

    sendPeripheralPacket(PID_DISPLAY, (int32_t)(selected_val * 100.0f));
    sendPeripheralPacket(PID_INDICATOR_LED, (int32_t)(nav_button_state * 100));

    vTaskDelay(pdMS_TO_TICKS(3000)); // Simulate a button press every 3s
  }
}

void DisplayTask(void *pvParameters) {
  for (;;) {
    float current_val = 0;
    if (nav_button_state == 1)
      current_val = sim_bowl1;
    else if (nav_button_state == 2)
      current_val = sim_bowl2;
    else
      current_val = (float)((int)sim_water_tank);

    sendPeripheralPacket(PID_DISPLAY, (int32_t)(current_val * 100.0f));
    sendPeripheralPacket(PID_INDICATOR_LED, (int32_t)(nav_button_state * 100));

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void LidHallTask(void *pvParameters) {
  for (;;) {
    // Lid Control: SID is ANALOG (0.0/1.0/3.0), PID is DIGITAL (0=idle,
    // 1=running)
    sendAnalogPacket(SID_LID1_HALL, (float)lid1_hall_state);
    sendPeripheralPacket(PID_LID1_STEPPER, lid1_motor_running ? 1 : 0);

    sendAnalogPacket(SID_LID2_HALL, (float)lid2_hall_state);
    sendPeripheralPacket(PID_LID2_STEPPER, lid2_motor_running ? 1 : 0);

    // Actuator Status (Periodic broadcast for UI visibility)
    sendPeripheralPacket(PID_CAMERA_STEPPER, camera_motor_running ? 1 : 0);
    sendPeripheralPacket(PID_CAMERA_SERVO, camera_servo_engaged ? 1 : 0);
    sendPeripheralPacket(PID_PUMP, pump_state ? 1 : 0);

    // Camera Homing Sensor (LMSW)
    uint8_t home_state = (abs(camera_angle) < 1.0f) ? 1 : 0;
    sendDigitalPacket(SID_HOME_SENSOR, home_state);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void EnvTask(void *pvParameters) {
  for (;;) {
    // Battery slowly draining
    float battery = 100.0f - fmod(sim_time * 0.01f, 100.0f);
    sendAnalogPacket(SID_BATTERY, battery);

    // Temp & Humidity
    sendAnalogPacket(SID_TEMPERATURE, 22.5f + 2.0f * sin(sim_time * 0.01f));
    sendAnalogPacket(SID_HUMIDITY, 45.0f + 10.0f * cos(sim_time * 0.01f));

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void ToggleTask(void *pvParameters) {
  for (;;) {
    power_switch_state = !power_switch_state;
    sendDigitalPacket(SID_POWER_SW, power_switch_state ? 1 : 0);

    // Lid 1: simulate transition → settled state
    lid1_open = !lid1_open;
    lid1_hall_state = 3; // Transition
    sendDigitalPacket(SID_LID1_HALL, 3);
    sendPeripheralPacket(PID_LID1_STEPPER, 1); // Motor running
    vTaskDelay(pdMS_TO_TICKS(1500));
    lid1_hall_state = lid1_open ? 1 : 0; // Settled
    sendDigitalPacket(SID_LID1_HALL, lid1_hall_state);
    sendPeripheralPacket(PID_LID1_STEPPER, 0); // Motor idle

    // Lid 2: simulate transition → settled state
    lid2_open = !lid2_open;
    lid2_hall_state = 3; // Transition
    sendDigitalPacket(SID_LID2_HALL, 3);
    sendPeripheralPacket(PID_LID2_STEPPER, 1); // Motor running
    vTaskDelay(pdMS_TO_TICKS(1500));
    lid2_hall_state = lid2_open ? 1 : 0; // Settled
    sendDigitalPacket(SID_LID2_HALL, lid2_hall_state);
    sendPeripheralPacket(PID_LID2_STEPPER, 0); // Motor idle

    // Pump on/off
    pump_state = !pump_state;
    sendPeripheralPacket(PID_PUMP, pump_state ? 1 : 0);

    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

void CameraTask(void *pvParameters) {
  for (;;) {
    camera_motor_running = !camera_motor_running;
    sendPeripheralPacket(PID_CAMERA_STEPPER, camera_motor_running ? 1 : 0);
    vTaskDelay(pdMS_TO_TICKS(8000));
  }
}

void CommandTask(void *pvParameters) {
  for (;;) {
    processIncomingCommands();
    checkActiveActions();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void checkActiveActions() {
  uint32_t now = millis();
  for (int i = 0; i < 8; i++) {
    if (active_actions[i].active &&
        (now - active_actions[i].start_ms >= active_actions[i].duration_ms)) {
      uint8_t pid = active_actions[i].id;

      // Emit motor idle + settled peripheral state on completion
      if (pid == PID_CAMERA_STEPPER) {
        camera_motor_running = false;
        sendPeripheralPacket(PID_CAMERA_STEPPER, 0); // Idle
      } else if (pid == PID_LID1_STEPPER) {
        lid1_motor_running = false;
        lid1_hall_state = lid1_open ? 1 : 0; // Settled state
        sendAnalogPacket(SID_LID1_HALL, (float)lid1_hall_state);
        sendPeripheralPacket(PID_LID1_STEPPER, 0); // Idle
      } else if (pid == PID_LID2_STEPPER) {
        lid2_motor_running = false;
        lid2_hall_state = lid2_open ? 1 : 0; // Settled state
        sendAnalogPacket(SID_LID2_HALL, (float)lid2_hall_state);
        sendPeripheralPacket(PID_LID2_STEPPER, 0); // Idle
      } else if (pid == PID_CAMERA_SERVO) {
        camera_servo_running = false;
        sendPeripheralPacket(PID_CAMERA_SERVO, camera_servo_engaged ? 1 : 0);
      } else if (pid == PID_PUMP) {
        sendPeripheralPacket(PID_PUMP, pump_state ? 1 : 0); // Final pump state
      }

      sendAckPacket(active_actions[i].id, active_actions[i].seq, ACK_SUCCESS);
      active_actions[i].active = false;
      Serial.printf("[FW] Async Action Complete: ID=%d SEQ=%d\n",
                    active_actions[i].id, active_actions[i].seq);
    }
  }
}

void registerAsyncAction(uint8_t id, uint8_t seq, uint32_t duration_ms,
                         int32_t val) {
  for (int i = 0; i < 8; i++) {
    if (!active_actions[i].active) {
      active_actions[i].id = id;
      active_actions[i].seq = seq;
      active_actions[i].start_ms = millis();
      active_actions[i].duration_ms = duration_ms;
      active_actions[i].final_val = val;
      active_actions[i].active = true;
      Serial.printf(
          "[FW] Registered Async Action: ID=%d SEQ=%d Duration=%dms\n", id, seq,
          duration_ms);
      return;
    }
  }
  // If no slot, send BUSY
  sendAckPacket(id, seq, ACK_BUSY);
}

// ── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ;
  }

  // Set builtin LED and Nav Button pin
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  pinMode(NAV_BUTTON_PIN, INPUT_PULLUP);

  // Initialize Tasks
  xTaskCreatePinnedToCore(HeartbeatTask, "Heartbeat", 2048, NULL, 1,
                          &HeartbeatTaskHandle, 1);
  xTaskCreatePinnedToCore(SensorsTask, "Sensors", 4096, NULL, 2,
                          &SensorsTaskHandle, 0);
  xTaskCreatePinnedToCore(NavButtonTask, "NavButton", 2048, NULL, 3,
                          &NavButtonTaskHandle, 1);
  xTaskCreatePinnedToCore(DisplayTask, "Display", 2048, NULL, 1,
                          &DisplayTaskHandle, 1);
  xTaskCreatePinnedToCore(LidHallTask, "LidHall", 2048, NULL, 2,
                          &LidHallTaskHandle, 1);
  xTaskCreatePinnedToCore(EnvTask, "Env", 2048, NULL, 1, &EnvTaskHandle, 0);
  xTaskCreatePinnedToCore(ToggleTask, "Toggle", 2048, NULL, 1,
                          &ToggleTaskHandle, 1);
  xTaskCreatePinnedToCore(CameraTask, "Camera", 2048, NULL, 1,
                          &CameraTaskHandle, 1);
  xTaskCreatePinnedToCore(CommandTask, "Command", 4096, NULL, 4,
                          &CommandTaskHandle, 1);
}

void loop() {
  // Empty - Logic is in FreeRTOS tasks
}

// ── Packet Senders & Protocol Implementation ────────────────────────────────

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
  int32_t payload = (int32_t)(value * 100.0f);
  sendRawPacket(priority, MSG_SENSOR_DATA, (uint8_t)id, payload);
}

void sendDigitalPacket(SensorID id, uint8_t state, uint8_t priority) {
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

void processIncomingCommands() {
  static uint8_t rx_buffer[128];
  static size_t rx_head = 0;

  while (Serial.available()) {
    if (rx_head < sizeof(rx_buffer)) {
      rx_buffer[rx_head++] = Serial.read();
    } else {
      for (size_t i = 0; i < sizeof(rx_buffer) - 1; i++) {
        rx_buffer[i] = rx_buffer[i + 1];
      }
      rx_buffer[sizeof(rx_buffer) - 1] = Serial.read();
    }
  }

  while (rx_head >= PACKET_SIZE) {
    size_t start_idx = 0;
    while (start_idx < rx_head && rx_buffer[start_idx] != START_BYTE) {
      start_idx++;
    }

    if (start_idx > 0) {
      rx_head -= start_idx;
      memmove(rx_buffer, rx_buffer + start_idx, rx_head);
    }

    if (rx_head < PACKET_SIZE)
      break;

    OroPacket *cand = (OroPacket *)rx_buffer;

    if (validate_packet_crc(cand)) {
      uint8_t type = GET_MSG_TYPE(cand->msg_type);
      int32_t val = extract_value_i32(cand->value);

      if (type == MSG_COMMAND) {
        uint8_t id = GET_ID(cand->id_seq);
        uint8_t seq = GET_SEQ(cand->id_seq);

        switch (id) {
        case PID_CAMERA_STEPPER:
          camera_angle = (float)val / 100.0f;
          if (camera_angle > 90.0f)
            camera_angle = 90.0f;
          if (camera_angle < -90.0f)
            camera_angle = -90.0f;
          encoder_ticks = (int32_t)camera_angle;
          camera_motor_running = true;
          sendPeripheralPacket(PID_CAMERA_STEPPER, 1); // Motor running
          registerAsyncAction(id, seq, 500, val);      // Settle after 0.5s
          break;

        case PID_PUMP:
          pump_state = (val > 0);
          sendPeripheralPacket(PID_PUMP, pump_state ? 1 : 0);
          registerAsyncAction(id, seq, 1000, val); // Simulate 1s priming
          break;

        case PID_LID1_STEPPER:
          lid1_open = (val > 0);
          lid1_motor_running = true;
          lid1_hall_state = 3; // Transition state
          sendAnalogPacket(SID_LID1_HALL, 3.0f, PRIO_HIGH);
          sendPeripheralPacket(PID_LID1_STEPPER, 1); // Motor running
          registerAsyncAction(id, seq, lid1_open ? 1500 : 800, val);   // Dynamic settle time
          break;

        case PID_LID2_STEPPER:
          lid2_open = (val > 0);
          lid2_motor_running = true;
          lid2_hall_state = 3; // Transition state
          sendAnalogPacket(SID_LID2_HALL, 3.0f, PRIO_HIGH);
          sendPeripheralPacket(PID_LID2_STEPPER, 1); // Motor running
          registerAsyncAction(id, seq, lid2_open ? 1600 : 900, val);   // Dynamic settle time
          break;

        case PID_DISPLAY:
          display_value = val / 100;
          sendPeripheralPacket(PID_DISPLAY, val);
          sendAckPacket(id, seq, ACK_SUCCESS); // Immediate
          break;

        case PID_INDICATOR_LED:
          led_state = (uint8_t)(val / 100);
          sendPeripheralPacket(PID_INDICATOR_LED, val);
          sendAckPacket(id, seq, ACK_SUCCESS); // Immediate
          break;

        case PID_CAMERA_SERVO:
          camera_servo_engaged = (val > 0);
          camera_servo_running = true;
          sendPeripheralPacket(PID_CAMERA_SERVO, camera_servo_engaged ? 1 : 0);
          registerAsyncAction(id, seq, 500, val); // Settle after 500ms
          break;

        default:
          // Unrecognized ID
          sendAckPacket(id, seq, ACK_INVALID);
          break;
        }
      }

      rx_head -= PACKET_SIZE;
      memmove(rx_buffer, rx_buffer + PACKET_SIZE, rx_head);
    } else {
      rx_head -= 1;
      memmove(rx_buffer, rx_buffer + 1, rx_head);
    }
  }
}