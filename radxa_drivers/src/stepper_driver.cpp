#include "radxa_drivers/stepper_driver.hpp"
#include <iostream>
#include <unistd.h>
#include <thread>

using namespace std::chrono;

// Half-step sequence (same as your script)
static const int HALFSTEP_SEQ[8][4] = {{1, 0, 0, 0}, 
                                       {1, 1, 0, 0}, 
                                       {0, 1, 0, 0},
                                       {0, 1, 1, 0}, 
                                       {0, 0, 1, 0}, 
                                       {0, 0, 1, 1},
                                       {0, 0, 0, 1}, 
                                       {1, 0, 0, 1}};

//
// Constructor 1 — lines already opened
//
Stepper::Stepper(int number_of_steps, gpiod_line *pin_a, gpiod_line *pin_b,
                 gpiod_line *pin_c, gpiod_line *pin_d) {
  this->number_of_steps = number_of_steps;
  this->step_number = 0;
  this->direction = 0;
  this->step_delay = 0;

  motor_pin_1 = pin_a;
  motor_pin_2 = pin_b;
  motor_pin_3 = pin_c;
  motor_pin_4 = pin_d;

  for (int i = 0; i < 4; i++)
    chips[i] = nullptr;

  gpiod_line_request_output(motor_pin_1, "stepper", 0);
  gpiod_line_request_output(motor_pin_2, "stepper", 0);
  gpiod_line_request_output(motor_pin_3, "stepper", 0);
  gpiod_line_request_output(motor_pin_4, "stepper", 0);

  last_step_time = steady_clock::now();
}

//
// Constructor 2 — open chips internally (your main use case)
//
Stepper::Stepper(int number_of_steps, const char *chip_a, int pin_a,
                 const char *chip_b, int pin_b, const char *chip_c, int pin_c,
                 const char *chip_d, int pin_d) {
  this->number_of_steps = number_of_steps;
  this->step_number = 0;
  this->direction = 0;
  this->step_delay = 0;

  chips[0] = gpiod_chip_open_by_name(chip_a);
  chips[1] = gpiod_chip_open_by_name(chip_b);
  chips[2] = gpiod_chip_open_by_name(chip_c);
  chips[3] = gpiod_chip_open_by_name(chip_d);

  motor_pin_1 = gpiod_chip_get_line(chips[0], pin_a);
  motor_pin_2 = gpiod_chip_get_line(chips[1], pin_b);
  motor_pin_3 = gpiod_chip_get_line(chips[2], pin_c);
  motor_pin_4 = gpiod_chip_get_line(chips[3], pin_d);

  gpiod_line_request_output(motor_pin_1, "stepper", 0);
  gpiod_line_request_output(motor_pin_2, "stepper", 0);
  gpiod_line_request_output(motor_pin_3, "stepper", 0);
  gpiod_line_request_output(motor_pin_4, "stepper", 0);

  last_step_time = steady_clock::now();
}

//
// Destructor
//
Stepper::~Stepper() {
  gpiod_line_set_value(motor_pin_1, 0);
  gpiod_line_set_value(motor_pin_2, 0);
  gpiod_line_set_value(motor_pin_3, 0);
  gpiod_line_set_value(motor_pin_4, 0);

  for (int i = 0; i < 4; i++)
    if (chips[i] != nullptr)
      gpiod_chip_close(chips[i]);
}

//
// RPM → step delay
//
void Stepper::setSpeed(long whatSpeed) {
  // microseconds between steps
  step_delay = 60L * 1000000L / number_of_steps / whatSpeed;
}

//
// Main stepping function
//
void Stepper::step(int steps_to_move) {
  int steps_left = abs(steps_to_move);
  direction = (steps_to_move > 0) ? 1 : 0;

  while (steps_left > 0) {
    auto now = steady_clock::now();
    auto us = duration_cast<microseconds>(now - last_step_time).count();

    if (us >= step_delay) {
      last_step_time = now;

      if (direction == 1) {
        step_number++;
        if (step_number == number_of_steps)
          step_number = 0;
      } else {
        if (step_number == 0)
          step_number = number_of_steps;
        step_number--;
      }

      stepMotor(step_number % 8); // half-step cycle
      steps_left--;
    } else {
      // Yield CPU to prevent busy-waiting
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }
}

//
// Coil energizing (IMPORTANT: your custom permutation {0,2,1,3})
//
void Stepper::stepMotor(int thisStep) {
  int coilA = HALFSTEP_SEQ[thisStep][0];
  int coilB = HALFSTEP_SEQ[thisStep][1];
  int coilC = HALFSTEP_SEQ[thisStep][2];
  int coilD = HALFSTEP_SEQ[thisStep][3];

  // Your exact mapping from working script:
  // pin13 <- coil A
  // pin15 <- coil C
  // pin33 <- coil B
  // pin35 <- coil D

  gpiod_line_set_value(motor_pin_1, coilA);
  gpiod_line_set_value(motor_pin_2, coilC);
  gpiod_line_set_value(motor_pin_3, coilB);
  gpiod_line_set_value(motor_pin_4, coilD);
}

void Stepper::disable() {
  gpiod_line_set_value(motor_pin_1, 0);
  gpiod_line_set_value(motor_pin_2, 0);
  gpiod_line_set_value(motor_pin_3, 0);
  gpiod_line_set_value(motor_pin_4, 0);
}