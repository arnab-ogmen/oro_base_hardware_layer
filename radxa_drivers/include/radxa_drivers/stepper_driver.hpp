#ifndef STEPPER_DRIVER_HPP
#define STEPPER_DRIVER_HPP

#include <chrono>
#include <gpiod.h>

class Stepper {
public:
  // Option 1: Provide already opened gpiod_line pointers for the 4 pins
  Stepper(int number_of_steps, gpiod_line *pin_a, gpiod_line *pin_b,
          gpiod_line *pin_c, gpiod_line *pin_d);

  // Option 2: Provide the chip name and line offset for each pin
  // Example: Stepper(2048, "gpiochip1", 6, "gpiochip1", 35, "gpiochip1", 7,
  // "gpiochip0", 38);
  Stepper(int number_of_steps, const char *chip_a, int pin_a,
          const char *chip_b, int pin_b, const char *chip_c, int pin_c,
          const char *chip_d, int pin_d);

  ~Stepper();

  // Set the speed in RPMs
  void setSpeed(long whatSpeed);

  // Move the motor steps_to_move steps. Positive is forward, negative is
  // backward.
  void step(int steps_to_move);

  // De-energize the motor coils to prevent heating and conserve power
  void disable();

private:
  void stepMotor(int thisStep);

  int direction; // Direction of rotation (1 for forward, 0 for backward)
  unsigned long step_delay; // Delay between steps in microseconds
  int number_of_steps;      // Total number of steps this motor can take per
                            // revolution
  int step_number;          // Which step the motor is on

  gpiod_chip
      *chips[4]; // Keep track of opened chips if we opened them internally
  gpiod_line *motor_pin_1;
  gpiod_line *motor_pin_2;
  gpiod_line *motor_pin_3;
  gpiod_line *motor_pin_4;

  std::chrono::time_point<std::chrono::steady_clock>
      last_step_time; // Timestamp for non-blocking step delay
};

#endif // STEPPER_DRIVER_HPP
