#include "radxa_drivers/camhead_rotation.hpp"
#include "radxa_drivers/gpio_sensor.hpp"
#include "radxa_drivers/stepper_driver.hpp"

#include <iostream>
#include <chrono>

namespace oro {

// --- Constructor / Destructor ---

CamHeadRotationNode::CamHeadRotationNode() {
    // Initialize switches wired directly to Radxa's GPIO pins
    try {
        limit_switch1_ = std::make_unique<GPIOSensor>("/dev/gpiochip0", 36, GPIOSensor::Bias::PULL_UP, "switch1");
        std::cout << "[CamHeadRotationNode] Initialized Limit Switch 1 (gpiochip0 pin 36)" << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "[CamHeadRotationNode] ERROR: Failed to initialize Limit Switch 1: " << e.what() << std::endl;
    }

    try {
        limit_switch2_ = std::make_unique<GPIOSensor>("/dev/gpiochip0", 39, GPIOSensor::Bias::PULL_UP, "switch2");
        std::cout << "[CamHeadRotationNode] Initialized Limit Switch 2 (gpiochip0 pin 39)" << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "[CamHeadRotationNode] ERROR: Failed to initialize Limit Switch 2: " << e.what() << std::endl;
    }

    try {
        home_sensor_ = std::make_unique<GPIOSensor>("/dev/gpiochip1", 37, GPIOSensor::Bias::PULL_DOWN, "encoder");
        std::cout << "[CamHeadRotationNode] Initialized Home Sensor (gpiochip1 pin 37)" << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "[CamHeadRotationNode] ERROR: Failed to initialize Home Sensor: " << e.what() << std::endl;
    }

    try {
        stepper_ = std::make_unique<Stepper>(TOTAL_STEPS, 
                        "gpiochip1", 6, 
                        "gpiochip1", 7, 
                        "gpiochip1", 35, 
                        "gpiochip0", 38);
        std::cout << "[CamHeadRotationNode] Initialized Stepper Motor on Radxa" << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "[CamHeadRotationNode] ERROR: Failed to initialize Stepper Motor: " << e.what() << std::endl;
    }
}

CamHeadRotationNode::~CamHeadRotationNode() {
    stop();
}

void CamHeadRotationNode::start() {
    if (running_.load()) return;
    running_ = true;
    if (stepper_) {
        stepper_thread_ = std::thread(&CamHeadRotationNode::stepper_thread_func, this);
    }
    std::cout << "[CamHeadRotationNode] Background threads started" << std::endl;
}

void CamHeadRotationNode::stop() {
    if (!running_.load()) return;
    running_ = false;
    
    stepper_cv_.notify_all();
    if (stepper_thread_.joinable()) {
        stepper_thread_.join();
    }
    std::cout << "[CamHeadRotationNode] Background threads stopped" << std::endl;
}

// --- Methods to read/update state (returning whether debounced value changed) ---

bool CamHeadRotationNode::update_limit_switch1(uint64_t current_ms, int& out_val) {
  // Rate-limit GPIO reads to 10ms to conserve CPU resources while maintaining responsiveness
  static uint64_t last_gpio_read_ms = 0;
  if (current_ms - last_gpio_read_ms < 10) {
    return false;
  }
  last_gpio_read_ms = current_ms;

  if (limit_switch1_) {
    try {
      int raw_val = limit_switch1_->read();
      int val = raw_val ? 0 : 1; // Invert active-low signal (1 = Pressed/Active, 0 = Released/Inactive)
      
      // Debounce logic with 50ms lockout
      if (val != limit_switch1_debounced_val_) {
        if (current_ms >= limit_switch1_lockout_until_ms_) {
          limit_switch1_debounced_val_ = val;
          limit_switch1_lockout_until_ms_ = current_ms + 50;
          out_val = limit_switch1_debounced_val_;
          return true;
        }
      }
    } catch (const std::exception &e) {
      static uint64_t last_err_ms = 0;
      if (current_ms - last_err_ms >= 5000) {
        std::cerr << "[CamHeadRotationNode] ERROR: Failed to read Limit Switch 1: " << e.what() << std::endl;
        last_err_ms = current_ms;
      }
    }
  }
  return false;
}

bool CamHeadRotationNode::update_limit_switch2(uint64_t current_ms, int& out_val) {
  // Rate-limit GPIO reads to 10ms to conserve CPU resources while maintaining responsiveness
  static uint64_t last_gpio_read_ms = 0;
  if (current_ms - last_gpio_read_ms < 10) {
    return false;
  }
  last_gpio_read_ms = current_ms;

  if (limit_switch2_) {
    try {
      int raw_val = limit_switch2_->read();
      int val = raw_val ? 0 : 1; // Invert active-low signal (1 = Pressed/Active, 0 = Released/Inactive)
      
      // Debounce logic with 50ms lockout
      if (val != limit_switch2_debounced_val_) {
        if (current_ms >= limit_switch2_lockout_until_ms_) {
          limit_switch2_debounced_val_ = val;
          limit_switch2_lockout_until_ms_ = current_ms + 50;
          out_val = limit_switch2_debounced_val_;
          return true;
        }
      }
    } catch (const std::exception &e) {
      static uint64_t last_err_ms = 0;
      if (current_ms - last_err_ms >= 5000) {
        std::cerr << "[CamHeadRotationNode] ERROR: Failed to read Limit Switch 2: " << e.what() << std::endl;
        last_err_ms = current_ms;
      }
    }
  }
  return false;
}

bool CamHeadRotationNode::update_home_sensor(uint64_t current_ms, int& out_val) {
  // Rate-limit GPIO reads to 10ms to conserve CPU resources while maintaining responsiveness
  static uint64_t last_gpio_read_ms = 0;
  if (current_ms - last_gpio_read_ms < 10) {
    return false;
  }
  last_gpio_read_ms = current_ms;

  if (home_sensor_) {
    try {
      int raw_val = home_sensor_->read();
      int val = raw_val; // Raw value maps directly (0 = Inactive, 1 = Active)
      
      // Debounce logic with 50ms lockout
      if (val != home_sensor_debounced_val_) {
        if (current_ms >= home_sensor_lockout_until_ms_) {
          home_sensor_debounced_val_ = val;
          home_sensor_lockout_until_ms_ = current_ms + 50;
          out_val = home_sensor_debounced_val_;
          return true;
        }
      }
    } catch (const std::exception &e) {
      static uint64_t last_err_ms = 0;
      if (current_ms - last_err_ms >= 5000) {
        std::cerr << "[CamHeadRotationNode] ERROR: Failed to read Home Sensor: " << e.what() << std::endl;
        last_err_ms = current_ms;
      }
    }
  }
  return false;
}

bool CamHeadRotationNode::update_stepper_status(int& out_running) {
  static int last_running_val = -1;
  int current_running_val = stepper_running_.load() ? 1 : 0;
  
  if (current_running_val != last_running_val) {
    last_running_val = current_running_val;
    out_running = current_running_val;
    return true;
  }
  return false;
}

bool CamHeadRotationNode::update_stepper_encoder(int32_t& out_ticks) {
  static int32_t last_ticks = -9999;
  int32_t current_ticks = static_cast<int32_t>(stepper_current_angle_.load());
  
  if (current_ticks != last_ticks) {
    last_ticks = current_ticks;
    out_ticks = current_ticks;
    return true;
  }
  return false;
}

// --- Internal Helper methods ---

bool CamHeadRotationNode::is_home_sensor_active() {
  return home_sensor_ && (home_sensor_->read() == 1);
}

bool CamHeadRotationNode::is_left_limit_switch_pressed() {
  return limit_switch1_ && (limit_switch1_->read() == 0);
}

bool CamHeadRotationNode::is_right_limit_switch_pressed() {
  return limit_switch2_ && (limit_switch2_->read() == 0);
}

void CamHeadRotationNode::settle_home_edge(int move_dir) {
  if (!stepper_) return;

  int away_dir = (move_dir > 0) ? -1 : 1;
  int max_steps = 42;
  int steps = 0;

  stepper_->setSpeed(5);

  // Back away until the encoder releases
  while (is_home_sensor_active() && steps < max_steps && running_.load()) {
    stepper_->step(away_dir);
    steps++;
  }

  // Approach slowly to the edge
  steps = 0;
  while (!is_home_sensor_active() && steps < max_steps && running_.load()) {
    stepper_->step(move_dir);
    steps++;
  }

  // If the sensor re-triggers, back off one step and approach again
  if (is_home_sensor_active() && running_.load()) {
    stepper_->step(away_dir);
    steps = 0;
    while (!is_home_sensor_active() && steps < max_steps && running_.load()) {
      stepper_->step(move_dir);
      steps++;
    }
  }

  // Move slightly deeper into the slider width
  if (is_home_sensor_active() && running_.load()) {
    const int final_offset_steps = 42;
    for (int i = 0; i < final_offset_steps && running_.load(); ++i) {
      if (!is_home_sensor_active()) {
        break;
      }
      stepper_->step(move_dir);
    }
  }
}

void CamHeadRotationNode::seek_cam_head_home_internal() {
  if (!stepper_) return;

  std::cout << "[StepperThread] Homing: moving anticlockwise towards Limit Switch 1..." << std::endl;
  stepper_->setSpeed(5);

  // 1. Rotate towards limit switch 1 until press is detected
  int step_count = 0;
  while (!is_left_limit_switch_pressed() && running_.load()) {
    stepper_->step(1);
    step_count++;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // 2. Once limit switch 1 is pressed, rotate in opposite direction until home is detected
  step_count = 0;
  while (!is_home_sensor_active() && running_.load()) {
    if (is_right_limit_switch_pressed()) {
      std::cout << "[StepperThread] WARNING: Hit Limit Switch 2 before finding Home Sensor! Aborting homing." << std::endl;
      break;
    }
    stepper_->step(-1);
    step_count++;
  }

  // 3. Settle precisely on the home edge
  if (is_home_sensor_active() && running_.load()) {
    std::cout << "[StepperThread] Home sensor active! Settling precisely on the home edge..." << std::endl;
    settle_home_edge(-1);
    stepper_current_angle_ = 0.0f;
    stepper_home_calibrated_ = true;
    std::cout << "[StepperThread] Homing complete! Camera calibrated at 0.0 degrees." << std::endl;
  } else {
    std::cout << "[StepperThread] Homing failed or interrupted." << std::endl;
  }
}

void CamHeadRotationNode::seek_cam_head_home() {
  stepper_running_ = true;
  seek_cam_head_home_internal();
  if (stepper_) {
    stepper_->disable();
  }
  stepper_running_ = false;
}

void CamHeadRotationNode::stepper_thread_func() {
  std::cout << "[StepperThread] Background motion thread running" << std::endl;

  // Always calibrate/home on startup
  seek_cam_head_home();

  while (running_.load(std::memory_order_relaxed)) {
    float target_angle = 0.0f;
    bool has_new_target = false;

    {
      std::unique_lock<std::mutex> lock(stepper_mtx_);
      stepper_cv_.wait_for(lock, std::chrono::milliseconds(100), [&]() {
        return stepper_target_updated_.load() || !running_.load();
      });

      if (!running_.load()) {
        break;
      }

      if (stepper_target_updated_.load()) {
        target_angle = stepper_target_angle_.load();
        stepper_target_updated_ = false;
        has_new_target = true;
      }
    }

    if (has_new_target) {
      if (target_angle == 999.0f) {
        stepper_running_ = true;
        seek_cam_head_home_internal();
        if (stepper_) {
          stepper_->disable();
        }
        stepper_running_ = false;
        continue;
      }

      // Clamp target to valid range
      if (target_angle > 90.0f) target_angle = 90.0f;
      if (target_angle < -90.0f) target_angle = -90.0f;

      stepper_running_ = true;

      // Only do physical switch homing if never calibrated since startup
      if (!stepper_home_calibrated_) {
        seek_cam_head_home_internal();
        stepper_current_angle_ = 0.0f;
        stepper_home_calibrated_ = true;
      }

      float current_angle = stepper_current_angle_.load();
      static const float ANGLE_PER_STEP = 180.0f / TOTAL_STEPS;

      if (stepper_ && stepper_home_calibrated_) {
        stepper_->setSpeed(5);

        if (target_angle > current_angle) {
          while (current_angle < target_angle && running_.load()) {
            if (is_right_limit_switch_pressed()) {
              current_angle = 90.0f;
              break;
            }
            stepper_->step(-1);
            current_angle += ANGLE_PER_STEP;
            if (current_angle > target_angle) {
              current_angle = target_angle;
              break;
            }
          }
        } else if (target_angle < current_angle) {
          while (current_angle > target_angle && running_.load()) {
            if (is_left_limit_switch_pressed()) {
              current_angle = -90.0f;
              break;
            }
            stepper_->step(1);
            current_angle -= ANGLE_PER_STEP;
            if (current_angle < target_angle) {
              current_angle = target_angle;
              break;
            }
          }
        }
        stepper_current_angle_ = current_angle;
      }

      if (stepper_) {
        stepper_->disable();
      }
      stepper_running_ = false;
    }
  }
}

void CamHeadRotationNode::set_stepper_target_angle(float target_angle) {
  {
    std::lock_guard<std::mutex> lock(stepper_mtx_);
    stepper_target_angle_ = target_angle;
    stepper_target_updated_ = true;
  }
  stepper_cv_.notify_all();
}

} // namespace oro