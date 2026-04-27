#include "oro_base_test_firmware.h"

HX711 scale1;
HX711 scale2;
HX711 scale3;

Adafruit_AHTX0 aht;
Stepper CAM_HEAD_Stepper(stepsPerRevolution, CAM_HEAD_STEPPER_A,
                         CAM_HEAD_STEPPER_C, CAM_HEAD_STEPPER_B,
                         CAM_HEAD_STEPPER_D); // IN1,IN3,IN2,IN4

// const uint8_t SEG_DONE[] = {
// 	SEG_B | SEG_C | SEG_D | SEG_E | SEG_G,           // d
// 	SEG_A | SEG_B | SEG_C | SEG_D | SEG_E | SEG_F,   // O
// 	SEG_C | SEG_E | SEG_G,                           // n
// 	SEG_A | SEG_D | SEG_E | SEG_F | SEG_G            // E
// 	};

// TM1637Display display1(I2C_SCL, I2C_SDA);
HT16K33 display;
TCA9535 TCA(0x20);

// static const uint8_t halfstep_sequence[8][4] = {
//   {1,0,0,0}, // A
//   {1,1,0,0}, // AB
//   {0,1,0,0}, // B
//   {0,1,1,0}, // BC
//   {0,0,1,0}, // C
//   {0,0,1,1}, // CD
//   {0,0,0,1}, // D
//   {1,0,0,1}  // DA
// };

static const uint8_t halfstep_sequence[8][4] = {
    {1, 0, 0, 0}, // A
    {1, 0, 1, 0}, // A+C
    {0, 0, 1, 0}, // C
    {0, 1, 1, 0}, // C+B
    {0, 1, 0, 0}, // B
    {0, 1, 0, 1}, // B+D
    {0, 0, 0, 1}, // D
    {1, 0, 0, 1}  // D+A
};

//--- Task Handlers-----
TaskHandle_t IRTaskHandle = NULL;
TaskHandle_t LoadCell1TaskHandle = NULL;
TaskHandle_t LoadCell2TaskHandle = NULL;
TaskHandle_t WaterTankLevelTaskHandle = NULL;
TaskHandle_t CamLimSwitch1TaskHandle = NULL;
TaskHandle_t CamLimSwitch2TaskHandle = NULL;
TaskHandle_t TemperatureTaskHandle = NULL;
TaskHandle_t NavButtonTaskHandle = NULL;
TaskHandle_t PwrButtonTaskHandle = NULL;
QueueHandle_t CAM_HEAD_stepperQueue;
TaskHandle_t CAM_HEAD_StepperTaskHandle = NULL;
TaskHandle_t pumpTaskHandle = NULL;
TaskHandle_t BatteryLevelTaskHandle = NULL;
TaskHandle_t CamEncoderTaskHandle = NULL;
TaskHandle_t LID1_StepperTaskHandle = NULL;
TaskHandle_t LID2_StepperTaskHandle = NULL;
// TODO: @Arnab Mondal Validate this task
TaskHandle_t LidHallTaskHandle = NULL;

// ----- freeRTOS Tasks -----

// ---------- IR TASK ----------
void IR_Task(void *pvParameters) {
  while (1) {
    int irState = digitalRead(FOOD_BOWL_IR);

    // if (irState == LOW) {
    //   // Serial.println("IR DETECTED");
    //   sendDigitalPacket(SID_WATER_BOWL, 0, PRIO_HIGH);
    //   // sendAnalogPacket(SID_WATER_BOWL, 0.0, PRIO_HIGH);

    // } else {
    //   // Serial.println("No IR");
    //   sendDigitalPacket(SID_WATER_BOWL, 1, PRIO_HIGH);
    //   // sendAnalogPacket(SID_WATER_BOWL, 1.0, PRIO_HIGH);

    // }
    sendDigitalPacket(SID_WATER_BOWL, irState == LOW ? 0 : 1, PRIO_HIGH);

    vTaskDelay(pdMS_TO_TICKS(1000)); // 1000ms delay
  }
}

void LoadCell1Task(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    if (scale1.is_ready()) {
      float v = scale1.get_units(3);
      // v -= Bowl1Weight;
      // Bowl1Weight = v;
      sendAnalogPacket(SID_LOAD_LEFT, v, PRIO_HIGH);
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void LoadCell2Task(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    if (scale2.is_ready()) {
      float v = scale2.get_units(3);
      // v -= Bowl2Weight;
      // Bowl2Weight = v;
      sendAnalogPacket(SID_LOAD_RIGHT, v, PRIO_HIGH);
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void WaterTankLevelTask(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    if (scale3.is_ready()) {
      float v = scale3.get_units(3);
      // v -= WaterTankWeight;
      // WaterTankWeight = v;
      sendAnalogPacket(SID_WATER_LEVEL, v, PRIO_HIGH);
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void CamLimSwitch1Task(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    int state = digitalRead(CAM_HEAD_ROT_LEFT);
    sendDigitalPacket(SID_LIMIT_SW1, !state, PRIO_HIGH);
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void CamLimSwitch2Task(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    int state = digitalRead(CAM_HEAD_ROT_RIGHT);
    sendDigitalPacket(SID_LIMIT_SW2, !state, PRIO_HIGH);
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void TemperatureTask(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    sensors_event_t temp, humidity;
    aht.getEvent(&temp, &humidity);

    sendAnalogPacket(SID_TEMPERATURE, temp.temperature, PRIO_MED);
    sendAnalogPacket(SID_HUMIDITY, humidity.relative_humidity, PRIO_MED);

    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void IRAM_ATTR navButtonISR() {
  uint32_t now = millis();

  // 200ms debounce (very safe for buttons)
  if (now - lastNavInterrupt > 200) {
    portENTER_CRITICAL_ISR(&navMux);
    navButtonEvent = true;
    lastNavInterrupt = now;
    portEXIT_CRITICAL_ISR(&navMux);
  }
}

void NavButtonTask(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    bool pressed = false;

    // safely read ISR flag
    portENTER_CRITICAL(&navMux);
    if (navButtonEvent) {
      navButtonEvent = false;
      pressed = true;
    }
    portEXIT_CRITICAL(&navMux);

    if (pressed) {
      nav_state++;
      if (nav_state > 3)
        nav_state = 0;

      sendDigitalPacket(SID_NAV_BUTTON, 1, PRIO_MED);
    }

    // LED state machine (unchanged)
    if (nav_state == 0) {
      digitalWrite(BOWL_LEFT_LED, LOW);
      digitalWrite(BOWL_RIGHT_LED, LOW);
      digitalWrite(WATER_TANK_LED, LOW);
      display.clear();
    } else if (nav_state == 1) {
      digitalWrite(BOWL_LEFT_LED, HIGH);
      digitalWrite(BOWL_RIGHT_LED, LOW);
      digitalWrite(WATER_TANK_LED, LOW);
      display.print("1234");
    } else if (nav_state == 2) {
      digitalWrite(BOWL_LEFT_LED, LOW);
      digitalWrite(BOWL_RIGHT_LED, HIGH);
      digitalWrite(WATER_TANK_LED, LOW);
      display.print("4321");

    } else if (nav_state == 3) {
      digitalWrite(BOWL_LEFT_LED, LOW);
      digitalWrite(BOWL_RIGHT_LED, LOW);
      digitalWrite(WATER_TANK_LED, HIGH);
      display.print("9999");
    }

    sendDigitalPacket(SID_NAV_BUTTON, pressed, PRIO_MED);
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void PwrButtonTask(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    int state = digitalRead(PWR_SW);
    sendDigitalPacket(SID_POWER_SW, !state, PRIO_CRIT);
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void CAM_HEAD_StepperTask(
    void *pvParameters) // The logic could be changed in future
{
  float target_angle;

  CAM_HEAD_Stepper.setSpeed(60);

  while (true) {
    // Wait for new target angle
    if (xQueueReceive(CAM_HEAD_stepperQueue, &target_angle, portMAX_DELAY) ==
        pdTRUE) {
      float delta_angle = target_angle - current_angle;

      int steps_to_move = (int)(delta_angle * STEPS_PER_DEGREE);

      Serial.print("Stepper moving to angle: ");
      Serial.println(target_angle);
      CAM_HEAD_Stepper.step(steps_to_move);
      current_angle = target_angle;
      sendPeripheralPacket(PID_CAMERA_STEPPER,
                           (int32_t)(current_angle * 100.0f), PRIO_HIGH);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void pumpTask(void *pvParameters) {

  while (true) {
    // Apply pump state to GPIO
    digitalWrite(PUMP_IO, pump_state ? HIGH : LOW);

    sendPeripheralPacket(PID_PUMP, pump_state ? 1 : 0, PRIO_HIGH);

    // Run every 50ms (enough for relay/MOSFET control)
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

float readVoltage() {
  // Use analogReadMilliVolts for accurate, calibrated reading on ESP32-S3
  uint32_t mv = analogReadMilliVolts(BATTERY_LEVEL_ADC);
  float voltage = (float)mv / 1000.0f; // convert to volts

  return voltage;
}

float voltageToPercent(float voltage) {
  float percent = (voltage / MAX_VOLTAGE) * 100.0;

  // clamp between 0 and 100
  if (percent > 100)
    percent = 100;
  if (percent < 0)
    percent = 0;

  return percent;
}

void BatteryLevelTask(void *pvParameters) {
  while (1) {
    float voltage = readVoltage();
    float percent = voltageToPercent(voltage);

    sendAnalogPacket(SID_BATTERY, percent, PRIO_LOW);

    vTaskDelay(1000 / portTICK_PERIOD_MS); // read every 1000ms
  }
}

void IRAM_ATTR handleEncoder() {
  unsigned long currentTime = micros();

  // debounce: ignore fast noise
  if (currentTime - lastInterruptTime > 20000) {
    portENTER_CRITICAL_ISR(&mux);
    pulseCount++;
    lastInterruptTime = currentTime;
    portEXIT_CRITICAL_ISR(&mux);
  }
}

void CamEncoderTask(void *pvParameters) {
  long lastCount = -1;

  while (1) {
    long safeCount;

    // safely copy shared variable
    portENTER_CRITICAL(&mux);
    safeCount = pulseCount;
    portEXIT_CRITICAL(&mux);

    if (safeCount != lastCount) {
      // Serial.print("Pulses: ");
      // Serial.println(safeCount);
      lastCount = safeCount;
    }

    sendEncoderPacket(SID_ENCODER, safeCount, PRIO_HIGH);

    vTaskDelay(100 / portTICK_PERIOD_MS); // read every 100ms
  }
}

void stepper_apply_state(const uint8_t index, const uint8_t state[4]) {
  if (index == 1) {
    TCA.write1(LID1_STEPPER_A_EXT, state[0]);
    TCA.write1(LID1_STEPPER_B_EXT, state[1]);
    TCA.write1(LID1_STEPPER_C_EXT, state[2]);
    TCA.write1(LID1_STEPPER_D_EXT, state[3]);
  } else if (index == 2) {
    TCA.write1(LID2_STEPPER_A_EXT, state[0]);
    TCA.write1(LID2_STEPPER_B_EXT, state[1]);
    TCA.write1(LID2_STEPPER_C_EXT, state[2]);
    TCA.write1(LID2_STEPPER_D_EXT, state[3]);
  }
}

void stepper_step(const uint8_t index, stepper_direction_t dir) {
  int idx = 0;
  for (uint32_t s = 0; s < steps_per_rev * 5; ++s) {
    stepper_apply_state(index, halfstep_sequence[idx]);
    delay(step_delay_ms);
    if (dir == STEPPER_DIR_FORWARD) {
      idx = (idx + 1) & 0x7;
    } else {
      idx = (idx + 7) & 0x7;
    }
  }
  // Stop motor and deactivate coils.
  const uint8_t off_state[4] = {0, 0, 0, 0};
  stepper_apply_state(index, off_state);
}

void LID1_StepperTask(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    // Wait until some other task tells us to run the motor
    if (LID1_Control == true) {

      sendPeripheralPacket(PID_LID1_STEPPER, 1, PRIO_HIGH); // Running
      if (stepper_direction == true) {
        // TRUE  -> Forward
        stepper_step(1, STEPPER_DIR_FORWARD);
      } else {
        // FALSE -> Reverse
        stepper_step(1, STEPPER_DIR_REVERSE);
      }

      LID1_Control = false; // consume request
      sendPeripheralPacket(PID_LID1_STEPPER, 0, PRIO_HIGH); // Idle
    }

    // Small delay so task does not hog CPU
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void LID2_StepperTask(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    // Wait until some other task tells us to run the motor
    if (LID2_Control == true) {

      sendPeripheralPacket(PID_LID2_STEPPER, 1, PRIO_HIGH); // Running
      if (stepper_direction == true) {
        // TRUE  -> Forward
        stepper_step(2, STEPPER_DIR_FORWARD);
      } else {
        // FALSE -> Reverse
        stepper_step(2, STEPPER_DIR_REVERSE);
      }

      LID2_Control = false; // consume request
      sendPeripheralPacket(PID_LID2_STEPPER, 0, PRIO_HIGH); // Idle
    }

    // Small delay so task does not hog CPU
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// TODO: @Arnab Mondal Validate this task
void LidHallTask(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    // Read Lid 1 Hall sensors from TCA expander
    // 0: Closed (Sensor 1 active), 1: Opened (Sensor 2 active)
    uint8_t lid1_state = 0;
    if (TCA.read1(LID1_HALL1_EXT) == LOW) {
      lid1_state = 0; // Closed
    } else if (TCA.read1(LID1_HALL2_EXT) == LOW) {
      lid1_state = 1; // Open
    }
    sendDigitalPacket(SID_LID1_HALL, lid1_state, PRIO_MED);

    // Read Lid 2 Hall sensors from TCA expander
    // 0: Closed (Sensor 3 active), 1: Opened (Sensor 4 active)
    uint8_t lid2_state = 0;
    if (TCA.read1(LID2_HALL1_EXT) == LOW) {
      lid2_state = 0; // Closed
    } else if (TCA.read1(LID2_HALL2_EXT) == LOW) {
      lid2_state = 1; // Open
    }
    sendDigitalPacket(SID_LID2_HALL, lid2_state, PRIO_MED);

    vTaskDelay(pdMS_TO_TICKS(100)); // Sample at 10Hz
  }
}

// ── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ;
  }
  Wire.begin(I2C_SDA, I2C_SCL);

  aht.begin();

  if (display.begin() == false) {
    // Serial.println("Device did not acknowledge! Freezing.");
    while (1)
      ;
  }
  if (!TCA.begin()) {
    Serial.println("GPIO Extender not found!");
    while (1)
      ;
  }

  scale1.begin(FOOD_BOWL_LEFT_DT, FOOD_BOWL_LEFT_SCK);
  scale1.set_offset(
      167612); // Used for Oro Base tank(Needs to be calibrated for Bowl)
  scale1.set_scale(387.515625);

  scale2.begin(FOOD_BOWL_RIGHT_DT, FOOD_BOWL_RIGHT_SCK);
  scale2.set_offset(
      167612); // Used for Oro Base tank(Needs to be calibrated for Bowl)
  scale2.set_scale(387.515625);

  scale3.begin(WATER_TANK_DT, WATER_TANK_SCK);
  scale3.set_offset(167612); // Used for Oro Base tank
  scale3.set_scale(387.515625);

  pinMode(FOOD_BOWL_IR, INPUT); // or INPUT_PULLUP if needed
  pinMode(CAM_HEAD_ROT_LEFT, INPUT);
  pinMode(CAM_HEAD_ROT_RIGHT, INPUT);
  // pinMode(NAV_SW, INPUT);
  pinMode(NAV_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(NAV_SW), navButtonISR, FALLING);
  pinMode(PWR_SW, INPUT);
  pinMode(PUMP_IO, OUTPUT);
  digitalWrite(PUMP_IO, LOW);

  pinMode(BOWL_LEFT_LED, OUTPUT);
  digitalWrite(BOWL_LEFT_LED, LOW);
  pinMode(BOWL_RIGHT_LED, OUTPUT);
  digitalWrite(BOWL_RIGHT_LED, LOW);
  pinMode(WATER_TANK_LED, OUTPUT);
  digitalWrite(WATER_TANK_LED, LOW);

  // Explicitly configure the battery ADC pin
  pinMode(BATTERY_LEVEL_ADC, INPUT);
  analogReadResolution(12);       // 12-bit ADC
  analogSetAttenuation(ADC_11db); // full 0–3.3V range

  pinMode(CAM_HEAD_ENCODER, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(CAM_HEAD_ENCODER), handleEncoder,
                  FALLING);

  TCA.pinMode1(LID1_HALL1_EXT, INPUT);
  TCA.pinMode1(LID1_HALL2_EXT, INPUT);
  TCA.pinMode1(LID2_HALL1_EXT, INPUT);
  TCA.pinMode1(LID2_HALL2_EXT, INPUT);

  TCA.pinMode1(LID1_STEPPER_A_EXT, OUTPUT);
  TCA.pinMode1(LID1_STEPPER_B_EXT, OUTPUT);
  TCA.pinMode1(LID1_STEPPER_C_EXT, OUTPUT);
  TCA.pinMode1(LID1_STEPPER_D_EXT, OUTPUT);
  TCA.pinMode1(LID2_STEPPER_A_EXT, OUTPUT);
  TCA.pinMode1(LID2_STEPPER_B_EXT, OUTPUT);
  TCA.pinMode1(LID2_STEPPER_C_EXT, OUTPUT);
  TCA.pinMode1(LID2_STEPPER_D_EXT, OUTPUT);

  CAM_HEAD_stepperQueue = xQueueCreate(5, sizeof(float));

  // Create task on Core 1 (recommended for Arduino loop tasks)
  xTaskCreatePinnedToCore(IR_Task, "IR Task", 2048, NULL, 4, &IRTaskHandle, 1);
  xTaskCreatePinnedToCore(NavButtonTask, "NavButtonTask", 2048, NULL, 2,
                          &NavButtonTaskHandle, 1);
  xTaskCreatePinnedToCore(PwrButtonTask, "PwrButtonTask", 1024, NULL, 9,
                          &PwrButtonTaskHandle, 1);

  // battery level task stack size can't be less than 2048 bytes
  xTaskCreatePinnedToCore(BatteryLevelTask, "BatteryLevelTask", 2048, NULL, 1,
                          &BatteryLevelTaskHandle, 1);

  xTaskCreatePinnedToCore(LoadCell1Task, "LoadCell1Task", 2048, NULL, 4,
                          &LoadCell1TaskHandle, 0);
  xTaskCreatePinnedToCore(LoadCell2Task, "LoadCell2Task", 2048, NULL, 4,
                          &LoadCell2TaskHandle, 0);
  xTaskCreatePinnedToCore(WaterTankLevelTask, "WaterTankLevelTask", 2048, NULL,
                          4, &WaterTankLevelTaskHandle, 0);
  xTaskCreatePinnedToCore(CamLimSwitch1Task, "CamLimSwitch1Task", 1024, NULL, 4,
                          &CamLimSwitch1TaskHandle, 0);
  xTaskCreatePinnedToCore(CamLimSwitch2Task, "CamLimSwitch2Task", 1024, NULL, 4,
                          &CamLimSwitch2TaskHandle, 0);
  xTaskCreatePinnedToCore(CamEncoderTask, "CamEncoderTask", 4096, NULL, 1,
                          &CamEncoderTaskHandle, 0);

  // Humidity and Temperature can be read with single task
  xTaskCreatePinnedToCore(TemperatureTask, "TemperatureTask", 4096, NULL, 2,
                          &TemperatureTaskHandle, 0);

  xTaskCreatePinnedToCore(CAM_HEAD_StepperTask, "CAM_HEAD_StepperTask", 2048,
                          NULL, 4, &CAM_HEAD_StepperTaskHandle, 1);
  xTaskCreatePinnedToCore(pumpTask, "Pump Task", 2048, NULL, 1, &pumpTaskHandle,
                          1);
  xTaskCreatePinnedToCore(LID1_StepperTask, "LID1_StepperTask", 2048, NULL, 4,
                          &LID1_StepperTaskHandle, 1);
  xTaskCreatePinnedToCore(LID2_StepperTask, "LID2_StepperTask", 2048, NULL, 4,
                          &LID2_StepperTaskHandle, 1);
  // TODO: @Arnab Mondal Validate this task
  xTaskCreatePinnedToCore(LidHallTask, "LidHallTask", 2048, NULL, 3,
                          &LidHallTaskHandle, 1);
}

// ── Main Loop ───────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // 10000ms: Heartbeat
  if (now - last_hb_update >= 1000) {
    last_hb_update = now;
    sendHeartbeat();
  }

  // // // Uncomment to test pump
  // pump_state = true;
  // vTaskDelay(10000 / portTICK_PERIOD_MS);
  // pump_state = false;
  // vTaskDelay(10000 / portTICK_PERIOD_MS);

  // LID1_Control = true;
  LID2_Control = true;
  stepper_direction = true; // forward
  vTaskDelay(5000 / portTICK_PERIOD_MS);
  stepper_direction = false; // reverse
  vTaskDelay(5000 / portTICK_PERIOD_MS);

  // LID1_Control = false;
  LID2_Control = false;
  vTaskDelay(5000 / portTICK_PERIOD_MS);

  processIncomingCommands();
}

// ── Packet Senders
// ──────────────────────────────────────────────────────────

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
          // sendPeripheralPacket(PID_CAMERA_STEPPER, (int32_t)(camera_angle *
          // 100.0f));
          xQueueSend(CAM_HEAD_stepperQueue, &camera_angle, 0);

        } else if (id == PID_PUMP) {
          pump_state = (val != 0);
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