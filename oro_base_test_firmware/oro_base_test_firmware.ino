#include "oro_base_test_firmware.h"

HX711 scale1;
HX711 scale2;
HX711 scale3;

// Adafruit_AHTX0 aht;
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
// HT16K33 display;
TCA9535 TCA(0x20);
Servo CAM_HEAD_SERVO;

// static void displayWeight4(float weight)
// {
//   // Display requirement: 4 chars total:
//   // - positive: "1234"
//   // - negative: "-123"
//   int v = (int)lroundf(weight);
//   if (v > 9999) v = 9999;
//   if (v < -999) v = -999;

//   char buf[5];
//   // right-aligned, space padded; negative will naturally be 4 chars like
//   "-123" snprintf(buf, sizeof(buf), "%4d", v);

//   display.clear();
//   display.print(buf);
//   sendPeripheralPacket(PID_DISPLAY, v*100, PRIO_MED);

// }

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

// static const uint8_t halfstep_sequence[8][4] = {
//   {1,0,0,0}, // A
//   {1,0,1,0}, // A+C
//   {0,0,1,0}, // C
//   {0,1,1,0}, // C+B
//   {0,1,0,0}, // B
//   {0,1,0,1}, // B+D
//   {0,0,0,1}, // D
//   {1,0,0,1}  // D+A
// };

static const uint8_t halfstep_sequence[8][4] = {
    {1, 0, 0, 0}, {1, 0, 1, 0}, {0, 0, 1, 0}, {0, 1, 1, 0},
    {0, 1, 0, 0}, {0, 1, 0, 1}, {0, 0, 0, 1}, {1, 0, 0, 1}}; // done

//--- Task Handlers-----
TaskHandle_t IRTaskHandle = NULL;
TaskHandle_t LoadCell1TaskHandle = NULL;
TaskHandle_t LoadCell2TaskHandle = NULL;
TaskHandle_t WaterTankLevelTaskHandle = NULL;
TaskHandle_t CamLimSwitch1TaskHandle = NULL;
TaskHandle_t CamLimSwitch2TaskHandle = NULL;
TaskHandle_t AmbientHTTaskHandle = NULL;
TaskHandle_t NavButtonTaskHandle = NULL;
TaskHandle_t PwrButtonTaskHandle = NULL;
QueueHandle_t CAM_HEAD_stepperQueue;
TaskHandle_t CAM_HEAD_StepperTaskHandle = NULL;
TaskHandle_t pumpTaskHandle = NULL;
TaskHandle_t BatteryLevelTaskHandle = NULL;
TaskHandle_t CamEncoderTaskHandle = NULL;
TaskHandle_t LID1_StepperTaskHandle = NULL;
TaskHandle_t LID2_StepperTaskHandle = NULL;
TaskHandle_t LID_HALLTaskHandle = NULL;
TaskHandle_t CAM_HEAD_HOMING_TaskHandle = NULL;
TaskHandle_t CAM_HEAD_SERVO_TaskHandle = NULL;

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
      Bowl1_Val = v;
      sendAnalogPacket(SID_LOAD_LEFT, v, PRIO_HIGH);
      if (nav_state == 1) {
        // displayWeight4(v);
      }
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void LoadCell2Task(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    if (scale2.is_ready()) {
      float v = scale2.get_units(3);
      Bowl2_Val = v;
      // v -= Bowl2Weight;
      // Bowl2Weight = v;
      sendAnalogPacket(SID_LOAD_RIGHT, v, PRIO_HIGH);
      if (nav_state == 2) {
        // displayWeight4(v);
      }
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

void WaterTankLevelTask(void *pvParameters) {
  (void)pvParameters;
  for (;;) {
    if (scale3.is_ready()) {
      float v = scale3.get_units(3);
      Tank_Val = v;
      // v -= WaterTankWeight;
      // WaterTankWeight = v;
      sendAnalogPacket(SID_WATER_LEVEL, v, PRIO_HIGH);
      if (nav_state == 3) {
        // displayWeight4(v);
      }
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

// void AmbientHTTask(void* pvParameters) {
//     (void)pvParameters;
//     for (;;) {
//       sensors_event_t temp, humidity;
//       aht.getEvent(&temp, &humidity);

//       sendAnalogPacket(SID_TEMPERATURE, temp.temperature, PRIO_MED);
//       sendAnalogPacket(SID_HUMIDITY, humidity.relative_humidity, PRIO_MED);

//       vTaskDelay(500 / portTICK_PERIOD_MS);
//     }
// }

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
      // display.clear();
      //  sendAnalogPacket(PID_INDICATOR_LED, 0.0f, PRIO_MED);
      sendPeripheralPacket(PID_INDICATOR_LED, 0, PRIO_MED);
      sendAnalogPacket(SID_NAV_BUTTON, 0.0f, PRIO_MED);
      sendPeripheralPacket(PID_DISPLAY, 0.00 * 100, PRIO_MED);

    } else if (nav_state == 1) {
      digitalWrite(BOWL_LEFT_LED, HIGH);
      digitalWrite(BOWL_RIGHT_LED, LOW);
      digitalWrite(WATER_TANK_LED, LOW);
      // display.print("1234");  // have to print actual values of load cell
      //  sendAnalogPacket(PID_INDICATOR_LED, 1.0f, PRIO_MED);

      sendPeripheralPacket(PID_INDICATOR_LED, 1, PRIO_MED);
      sendAnalogPacket(SID_NAV_BUTTON, 1.0f, PRIO_MED);

    } else if (nav_state == 2) {
      digitalWrite(BOWL_LEFT_LED, LOW);
      digitalWrite(BOWL_RIGHT_LED, HIGH);
      digitalWrite(WATER_TANK_LED, LOW);
      // display.print("4321");
      //  sendAnalogPacket(PID_INDICATOR_LED, 2.0f, PRIO_MED);
      sendPeripheralPacket(PID_INDICATOR_LED, 2, PRIO_MED);
      sendAnalogPacket(SID_NAV_BUTTON, 2.0f, PRIO_MED);

    } else if (nav_state == 3) {
      digitalWrite(BOWL_LEFT_LED, LOW);
      digitalWrite(BOWL_RIGHT_LED, LOW);
      digitalWrite(WATER_TANK_LED, HIGH);
      // display.print("9999");
      // sendPeripheralPacket(PID_INDICATOR_LED, 3, PRIO_MED);
      sendPeripheralPacket(PID_INDICATOR_LED, 3, PRIO_MED);

      sendAnalogPacket(SID_NAV_BUTTON, 3.0f, PRIO_MED);
    }

    // sendAnalogPacket(SID_NAV_BUTTON, pressed, PRIO_MED);
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
  bool running = false;

  while (true) {
    // Wait for new target angle
    if (xQueueReceive(CAM_HEAD_stepperQueue, &target_angle,
                      pdMS_TO_TICKS(100)) == pdTRUE) {
      float delta_angle = target_angle - current_angle;

      int steps_to_move = (int)(delta_angle * STEPS_PER_DEGREE);

      Serial.print("Stepper moving to angle: ");
      Serial.println(target_angle);

      // MW expects PID_CAMERA_STEPPER to behave like a DIGITAL motor state:
      // 1 = running, 0 = stopped. (Angle is already available via
      // encoder/sensors.)
      if (!running && steps_to_move != 0) {
        running = true;
        sendPeripheralPacket(PID_CAMERA_STEPPER, 1, PRIO_HIGH);
      }

      CAM_HEAD_Stepper.step(steps_to_move);
      current_angle = target_angle;

      if (running) {
        running = false;
        sendPeripheralPacket(PID_CAMERA_STEPPER, 0, PRIO_HIGH);
      }
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
    } else if (dir == STEPPER_DIR_REVERSE) {
      idx = (idx + 7) & 0x7;
    } else if (dir == STEPPER_DIR_STOP) {
      const uint8_t off_state[4] = {0, 0, 0, 0};
      stepper_apply_state(index, off_state);
      return;
    }
  }
  // Stop motor and deactivate coils.
  const uint8_t off_state[4] = {0, 0, 0, 0};
  stepper_apply_state(index, off_state);
}

void LID1_StepperTask(void *pvParameters) {
  (void)pvParameters;

  Lid1StepperCommand_t cmd;
  bool running = false;

  for (;;) {
    // Backward compatibility: if someone still toggles the old flag,
    // convert it into a queued command (so it doesn't rely on pulse timing).
    if (LID1_Control) {
      LID1_Control = false; // consume request
      cmd.dir = stepper_direction ? STEPPER_DIR_REVERSE : STEPPER_DIR_FORWARD;
      cmd.id = 0xFF; // No ACK for legacy triggers
      (void)xQueueSend(lid1StepperQueue, &cmd, portMAX_DELAY);
    }

    // Primary control path: wait for queued commands (lets CW then CCW work
    // reliably)
    if (xQueueReceive(lid1StepperQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (!running) {
        running = true;
        // 1 = running
        sendPeripheralPacket(PID_LID1_STEPPER, 1, PRIO_LOW);
      }
      stepper_step(1, cmd.dir);
      if (running) {
        running = false;
        // 0 = stopped
        sendPeripheralPacket(PID_LID1_STEPPER, 0, PRIO_LOW);

        // Late ACK: only if we have a valid command context
        if (cmd.id != 0xFF) {
          sendAckPacket(cmd.id, cmd.seq, ACK_SUCCESS);
        }
      }
    }

    // Small delay so task does not hog CPU
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void LID2_StepperTask(void *pvParameters) {
  (void)pvParameters;

  Lid2StepperCommand_t cmd;
  bool running = false;

  for (;;) {
    // Backward compatibility: if someone still toggles the old flag,
    // convert it into a queued command (so it doesn't rely on pulse timing).
    if (LID2_Control) {
      LID2_Control = false; // consume request
      cmd.dir = stepper_direction ? STEPPER_DIR_REVERSE : STEPPER_DIR_FORWARD;
      cmd.id = 0xFF; // No ACK for legacy triggers
      (void)xQueueSend(lid2StepperQueue, &cmd, portMAX_DELAY);
    }

    // Primary control path: wait for queued commands (lets CW then CCW work
    // reliably)
    if (xQueueReceive(lid2StepperQueue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
      if (!running) {
        running = true;
        // 1 = running
        sendPeripheralPacket(PID_LID2_STEPPER, 1, PRIO_LOW);
      }
      stepper_step(2, cmd.dir);
      if (running) {
        running = false;
        // 0 = stopped
        sendPeripheralPacket(PID_LID2_STEPPER, 0, PRIO_LOW);

        // Late ACK: only if we have a valid command context
        if (cmd.id != 0xFF) {
          sendAckPacket(cmd.id, cmd.seq, ACK_SUCCESS);
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void LID_HALLTask(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    int hall1 = TCA.read1(LID1_HALL1_EXT);
    int hall2 = TCA.read1(LID1_HALL2_EXT);
    int hall3 = TCA.read1(LID2_HALL1_EXT);
    int hall4 = TCA.read1(LID2_HALL2_EXT);

    if (hall1 == HIGH) {
      sendAnalogPacket(SID_LID1_HALL, 0.0f, PRIO_MED);
      // To stop the Lid1 stepper, send a command to lid1StepperQueue with a
      // 'STOP' directive, if the handler supports a STOP state or value. Let's
      // check the LID1_StepperTask handler: If handler only understands dir ==
      // STEPPER_DIR_FORWARD or STEPPER_DIR_REVERSE, you must extend handler to
      // also support a STOP value, e.g., (stepper_direction_t)2.

      // Here we create and send a STOP command, corresponding to a new STOP
      // value.
      Lid1StepperCommand_t stop_cmd;
      stop_cmd.dir = (stepper_direction_t)2;
      stop_cmd.id = 0xFF; // No ACK for sensor triggers
      xQueueSend(lid1StepperQueue, &stop_cmd, 0);
    } else if (hall2 == HIGH) {
      sendAnalogPacket(SID_LID1_HALL, 1.0f, PRIO_MED);
      Lid1StepperCommand_t stop_cmd;
      stop_cmd.dir = (stepper_direction_t)2;
      stop_cmd.id = 0xFF; // No ACK for sensor triggers
      xQueueSend(lid1StepperQueue, &stop_cmd, 0);
    } else {
      sendAnalogPacket(SID_LID1_HALL, 3.0f, PRIO_MED);
    }

    if (hall3 == HIGH) {
      sendAnalogPacket(SID_LID2_HALL, 0.0f, PRIO_MED);
      Lid2StepperCommand_t stop_cmd;
      stop_cmd.dir = (stepper_direction_t)2;
      stop_cmd.id = 0xFF; // No ACK for sensor triggers
      xQueueSend(lid2StepperQueue, &stop_cmd, 0);
    } else if (hall4 == HIGH) {
      sendAnalogPacket(SID_LID2_HALL, 1.0f, PRIO_MED);
      Lid2StepperCommand_t stop_cmd;
      stop_cmd.dir = (stepper_direction_t)2;
      stop_cmd.id = 0xFF; // No ACK for sensor triggers
      xQueueSend(lid2StepperQueue, &stop_cmd, 0);
    } else {
      sendAnalogPacket(SID_LID2_HALL, 3.0f, PRIO_MED);
    }

    // sendDigitalPacket(SID_LID1_HALL, hall1 && hall2, PRIO_MED);
  }

  vTaskDelay(pdMS_TO_TICKS(1000));
}

void CAM_HEAD_HOMING_Task(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    int homingState = TCA.read1(CAM_HEAD_HOMING_EXT);

    sendDigitalPacket(SID_HOME_SENSOR, homingState, PRIO_HIGH);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void MoveCamServo(float angle) {
  ServoCommand_t cmd;
  cmd.target_angle = angle;

  xQueueSend(camServoQueue, &cmd, portMAX_DELAY); // portMAX_DELAY
}

void ServoWriteAngle(float angle) {
  // Clamp angle
  if (angle < 0)
    angle = 0;
  if (angle > 180)
    angle = 180;

  // Convert angle → pulse width (µs)
  // SG90 typical safe control range is closer to ~1000–2000 µs.
  // Wide ranges (e.g., 500–2500 µs) can drive it into mechanical end-stops.
  const float SERVO_MIN_US = 1000.0f;
  const float SERVO_MAX_US = 2000.0f;
  float pulseWidth =
      SERVO_MIN_US + (angle / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US);

  // Convert µs → duty (LEDC resolution bits)
  const uint32_t maxDuty = (1UL << SERVO_PWM_RES) - 1UL;
  uint32_t duty = (uint32_t)((pulseWidth / 20000.0f) * (float)maxDuty);

  // Newer Arduino-ESP32 LEDC API: ledcWrite() is pin-based.
  ledcWrite(CAM_HEAD_SERVO_PIN, duty);
}

void CAM_HEAD_SERVO_Task(void *pvParameters) {
  (void)pvParameters;

  ServoCommand_t cmd;
  float current_angle_servo = 0.0f;

  // Move servo to initial position
  ServoWriteAngle(current_angle_servo);
  // sendPeripheralPacket(PID_CAMERA_SERVO, servo_state, PRIO_MED);

  while (1) {
    // Wait for command (timeout lets task stay alive even if no command)
    if (xQueueReceive(camServoQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
      float target = cmd.target_angle;

      // Clamp for safety
      if (target < 0)
        target = 0;
      if (target > MAX_ANGLE)
        target = MAX_ANGLE;

      // Smooth movement
      while (fabs(current_angle_servo - target) > 0.5f) {
        if (current_angle_servo < target)
          current_angle_servo += 1.0f;
        else
          current_angle_servo -= 1.0f;

        ServoWriteAngle(current_angle_servo); // ⭐ IMPORTANT CHANGE
        sendPeripheralPacket(PID_CAMERA_SERVO, true, PRIO_MED);
        vTaskDelay(pdMS_TO_TICKS(20)); // movement speed
      }

      current_angle_servo = target;
      ServoWriteAngle(current_angle_servo); // ⭐ IMPORTANT CHANGE
    }
    // servo_state = false;
    sendPeripheralPacket(PID_CAMERA_SERVO, false, PRIO_MED);
    vTaskDelay(pdMS_TO_TICKS(100)); // Update at least every 100ms
  }
}

// ── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ;
  }
  Wire.begin(I2C_SDA, I2C_SCL);

  // aht.begin();

  // if (display.begin() == false)
  // {
  //   //Serial.println("Device did not acknowledge! Freezing.");
  //   while (1);
  // }
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
  TCA.pinMode1(CAM_HEAD_HOMING_EXT, INPUT);

  TCA.pinMode1(LID1_STEPPER_A_EXT, OUTPUT);
  TCA.pinMode1(LID1_STEPPER_B_EXT, OUTPUT);
  TCA.pinMode1(LID1_STEPPER_C_EXT, OUTPUT);
  TCA.pinMode1(LID1_STEPPER_D_EXT, OUTPUT);
  TCA.pinMode1(LID2_STEPPER_A_EXT, OUTPUT);
  TCA.pinMode1(LID2_STEPPER_B_EXT, OUTPUT);
  TCA.pinMode1(LID2_STEPPER_C_EXT, OUTPUT);
  TCA.pinMode1(LID2_STEPPER_D_EXT, OUTPUT);

  // Configure PWM for servo (50 Hz) using LEDC (Arduino-ESP32 v3 style API)
  // ledcAttach() sets up the channel internally and binds it to the pin.
  if (!ledcAttach(CAM_HEAD_SERVO_PIN, SERVO_PWM_FREQ, SERVO_PWM_RES)) {
    Serial.println("ERROR: LEDC attach failed for servo pin");
  }

  CAM_HEAD_stepperQueue = xQueueCreate(5, sizeof(float));
  camServoQueue = xQueueCreate(5, sizeof(ServoCommand_t));
  lid2StepperQueue = xQueueCreate(5, sizeof(Lid2StepperCommand_t));
  lid1StepperQueue = xQueueCreate(5, sizeof(Lid1StepperCommand_t));

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
  xTaskCreatePinnedToCore(CamLimSwitch1Task, "CamLimSwitch1Task", 2048, NULL, 4,
                          &CamLimSwitch1TaskHandle, 0);
  xTaskCreatePinnedToCore(CamLimSwitch2Task, "CamLimSwitch2Task", 2048, NULL, 4,
                          &CamLimSwitch2TaskHandle, 0);
  xTaskCreatePinnedToCore(CAM_HEAD_HOMING_Task, "CAM_HEAD_HOMING_Task", 2048,
                          NULL, 5, &CAM_HEAD_HOMING_TaskHandle, 0);
  xTaskCreatePinnedToCore(CamEncoderTask, "CamEncoderTask", 4096, NULL, 1,
                          &CamEncoderTaskHandle, 0);
  xTaskCreatePinnedToCore(LID_HALLTask, "LID_HALLTask", 2048, NULL, 1,
                          &LID_HALLTaskHandle, 0);

  // Humidity and Temperature can be read with single task
  // xTaskCreatePinnedToCore(AmbientHTTask, "AmbientHTTask", 4096, NULL, 2,
  // &AmbientHTTaskHandle, 0);

  xTaskCreatePinnedToCore(CAM_HEAD_StepperTask, "CAM_HEAD_StepperTask", 2048,
                          NULL, 4, &CAM_HEAD_StepperTaskHandle, 1);
  xTaskCreatePinnedToCore(pumpTask, "Pump Task", 2048, NULL, 1, &pumpTaskHandle,
                          1);
  xTaskCreatePinnedToCore(LID1_StepperTask, "LID1_StepperTask", 2048, NULL, 4,
                          &LID1_StepperTaskHandle, 1);
  xTaskCreatePinnedToCore(LID2_StepperTask, "LID2_StepperTask", 2048, NULL, 4,
                          &LID2_StepperTaskHandle, 1);
  xTaskCreatePinnedToCore(CAM_HEAD_SERVO_Task, "CAM_HEAD_SERVO_Task", 2048,
                          NULL, 4, &CAM_HEAD_SERVO_TaskHandle, 1);

  // ServoWriteAngle(0); // Move servo to initial position
  // vTaskDelay(2000 / portTICK_PERIOD_MS);
  // ServoWriteAngle(90); // Move servo to test position
  // vTaskDelay(2000 / portTICK_PERIOD_MS);
}

// ── Main Loop ───────────────────────────────────────────────────────────────
void loop() {
  unsigned long now = millis();

  // 10000ms: Heartbeat
  if (now - last_hb_update >= 1000) {
    last_hb_update = now;
    sendHeartbeat();
  }

  // // // // Uncomment to test pump
  // pump_state = true;
  // vTaskDelay(3000 / portTICK_PERIOD_MS);
  // pump_state = false;
  // // vTaskDelay(10000 / portTICK_PERIOD_MS);

  // //LID1_Control = true;
  // // stepper_direction = true; // forward
  // //--- RUN FORWARD ---

  // // forward

  // Lid1StepperCommand_t cmd2;

  // cmd2.dir = STEPPER_DIR_FORWARD;
  // xQueueSend(lid1StepperQueue, &cmd2, portMAX_DELAY);
  // vTaskDelay(pdMS_TO_TICKS(3000));

  // // reverse
  // cmd2.dir = STEPPER_DIR_REVERSE;
  // xQueueSend(lid1StepperQueue, &cmd2, portMAX_DELAY);
  // vTaskDelay(pdMS_TO_TICKS(3000));

  // Lid2StepperCommand_t cmd1;

  // cmd1.dir = STEPPER_DIR_FORWARD;
  // xQueueSend(lid2StepperQueue, &cmd1, portMAX_DELAY);
  // vTaskDelay(pdMS_TO_TICKS(3000));

  // // reverse
  // cmd1.dir = STEPPER_DIR_REVERSE;
  // xQueueSend(lid2StepperQueue, &cmd1, portMAX_DELAY);
  // vTaskDelay(pdMS_TO_TICKS(3000));

  // MoveCamServo(180);
  // vTaskDelay(2000 / portTICK_PERIOD_MS);

  // MoveCamServo(0);
  // vTaskDelay(2000 / portTICK_PERIOD_MS);

  // camera_angle = 45.0f;
  // xQueueSend(CAM_HEAD_stepperQueue, &camera_angle, portMAX_DELAY);
  // vTaskDelay(3000 / portTICK_PERIOD_MS);

  // Process incoming UART command packets (pump/lids/servo/camera stepper).
  processIncomingCommands();
  vTaskDelay(pdMS_TO_TICKS(1));
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

        bool deferred_ack = false;

        switch (id) {
        case PID_CAMERA_STEPPER:
          // Set target angle within range [+90, -90]
          camera_angle = (float)val / 100.0f;
          if (camera_angle > 90.0f)
            camera_angle = 90.0f;
          if (camera_angle < -90.0f)
            camera_angle = -90.0f;

          // Encoder ticks = exactly the angle (1 tick per degree)
          encoder_ticks = (int32_t)camera_angle;
          xQueueSend(CAM_HEAD_stepperQueue, &camera_angle, portMAX_DELAY);
          break;

        case PID_PUMP:
          pump_state = ((float)val != 0);
          break;

        case PID_CAMERA_SERVO:
          servo_state = ((float)val != 0);
          if (servo_state) {
            MoveCamServo(90.0f);
          } else {
            MoveCamServo(0.0f);
          }
          break;

        case PID_LID1_STEPPER: {
          Lid1StepperCommand_t cmd;
          cmd.dir = (val != 0) ? STEPPER_DIR_REVERSE : STEPPER_DIR_FORWARD;
          cmd.seq = seq;
          cmd.id = id;
          xQueueSend(lid1StepperQueue, &cmd, portMAX_DELAY);
          deferred_ack = true;
        } break;

        case PID_LID2_STEPPER: {
          Lid2StepperCommand_t cmd;
          cmd.dir = (val != 0) ? STEPPER_DIR_REVERSE : STEPPER_DIR_FORWARD;
          cmd.seq = seq;
          cmd.id = id;
          xQueueSend(lid2StepperQueue, &cmd, portMAX_DELAY);
          deferred_ack = true;
        } break;

        default:
          // Handle unknown PID if necessary
          break;
        }

        if (!deferred_ack) {
          sendAckPacket(id, seq, ACK_SUCCESS);
        }
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