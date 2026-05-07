#include "oro_protocol.h"
#include <Adafruit_AHTX0.h>
#include <Arduino.h>
#include <HX711.h>
#include <Stepper.h>
#include <Wire.h>
#include <math.h>
// #include <TM1637Display.h>
#include <ESP32Servo.h>
#include <SparkFun_Alphanumeric_Display.h>
#include <TCA9555.h>

using namespace oro;

#ifndef LED_BUILTIN
// #define LED_BUILTIN 2
#endif

#define FOOD_BOWL_IR 3
#define FOOD_BOWL_LEFT_SCK 13
#define FOOD_BOWL_LEFT_DT 14
#define FOOD_BOWL_RIGHT_SCK 11
#define FOOD_BOWL_RIGHT_DT 12
#define WATER_TANK_SCK 46
#define WATER_TANK_DT 9
#define CAM_HEAD_ROT_LEFT 39
#define CAM_HEAD_ROT_RIGHT 38
#define I2C_SCL 5
#define I2C_SDA 4
#define NAV_SW 6
#define PWR_SW 7
#define CAM_HEAD_ENCODER 21
#define BATTERY_LEVEL_ADC 1

#define LID1_HALL1_EXT 12
#define LID1_HALL2_EXT 13
#define LID2_HALL1_EXT 0
#define LID2_HALL2_EXT 1

#define CAM_HEAD_HOMING_EXT 14

#define LID1_STEPPER_A_EXT 11
#define LID1_STEPPER_B_EXT 9
#define LID1_STEPPER_C_EXT 10
#define LID1_STEPPER_D_EXT 8

#define LID2_STEPPER_A_EXT 4
#define LID2_STEPPER_B_EXT 6
#define LID2_STEPPER_C_EXT 5
#define LID2_STEPPER_D_EXT 7

#define PUMP_IO 10
#define CAM_HEAD_STEPPER_A 42
#define CAM_HEAD_STEPPER_B 45
#define CAM_HEAD_STEPPER_C 48
#define CAM_HEAD_STEPPER_D 47
#define CAM_HEAD_SERVO_PIN 40 // TBD
#define BOWL_LEFT_LED 17      ////
#define BOWL_RIGHT_LED 16
#define WATER_TANK_LED 15

// ── Timing Variables ────────────────────────────────────────────────────────
unsigned long last_fast_update = 0; // 100ms
unsigned long last_slow_update = 0; // 1000ms
unsigned long last_env_update = 0;  // 5000ms
unsigned long last_toggle_5s = 0;   // 5000ms toggle
unsigned long last_toggle_8s = 0;   // 8000ms toggle
unsigned long last_display_up = 0;  // 2000ms display update
unsigned long last_hb_update = 0;   // 10000ms heartbeat

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
// bool pump_state = false;
bool sim_water_bowl = false;
bool camera_motor_state = false;

int32_t display_value = 1234;
uint8_t led_state = 0;

float sim_bowl1 = 0;
float sim_bowl2 = 0;
float sim_water_tank = 5.0f;
// float sim_water_bowl = 0.5f;

const int stepsPerRevolution = 500;
#define MAX_ANGLE 90.0f
#define STEPS_PER_DEGREE (stepsPerRevolution / 360.0f)
volatile float current_angle = 0.0f;
volatile bool pump_state = false;
#define MAX_VOLTAGE 3.1 // 100% reference voltage
volatile long pulseCount = 0;
volatile unsigned long lastInterruptTime = 0;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

#define TEST_DELAY 2000

volatile uint8_t nav_state = 0;
volatile bool navButtonEvent = false;
volatile uint32_t lastNavInterrupt = 0;

portMUX_TYPE navMux = portMUX_INITIALIZER_UNLOCKED;

float step_delay_ms = 1.0; // 0.5
uint32_t steps_per_rev = 500;

typedef enum {
  STEPPER_DIR_FORWARD = 0,
  STEPPER_DIR_REVERSE = 1,
  STEPPER_DIR_STOP = 2,
} stepper_direction_t;

volatile bool LID1_Control = false;
volatile bool LID2_Control = false;
volatile bool stepper_direction = false; // false = forward, true = reverse

float Bowl1_Val = 0;
float Bowl2_Val = 0;
float Tank_Val = 0;

typedef struct {
  stepper_direction_t dir;
  uint8_t seq;
  uint8_t id;
} Lid2StepperCommand_t;

typedef struct {
  stepper_direction_t dir;
  uint8_t seq;
  uint8_t id;
} Lid1StepperCommand_t;

typedef struct {
  float target_angle;
} ServoCommand_t;

QueueHandle_t camServoQueue;
QueueHandle_t lid2StepperQueue;
QueueHandle_t lid1StepperQueue;

#define SERVO_PWM_CHANNEL 0
#define SERVO_PWM_FREQ 50
// Note: On ESP32-S3 (and most ESP32 variants) LEDC duty resolution is typically
// max 14 bits.
#define SERVO_PWM_RES 14

bool servo_state = false;

// ── Forward Declarations ────────────────────────────────────────────────────
void sendAnalogPacket(SensorID id, float value, uint8_t priority = PRIO_LOW);
void sendDigitalPacket(SensorID id, uint8_t state, uint8_t priority = PRIO_LOW);
void sendEncoderPacket(SensorID id, int32_t ticks, uint8_t priority = PRIO_MED);
void sendPeripheralPacket(PeripheralID id, int32_t state,
                          uint8_t priority = PRIO_LOW);
void sendHeartbeat();
void processIncomingCommands();

void sendRawPacket(uint8_t priority, uint8_t type, uint8_t id, int32_t payload);
void sendAckPacket(uint8_t id, uint8_t seq, int32_t payload);

// ── Sequence Numbers
//  One sequence array for Sensors, one for Peripherals
uint8_t seq_nums_sensor[SID_COUNT] = {0};
uint8_t seq_nums_periph[16] = {0};