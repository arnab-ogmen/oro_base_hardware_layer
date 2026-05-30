#ifndef TREAT_DISPENSER_HPP
#define TREAT_DISPENSER_HPP

#include <string>
#include <gpiod.h>
#include <atomic>
#include <thread>
#include <chrono>

namespace oro {

enum class DispenseResult {
    SUCCESS,
    NOT_INITIALIZED,
    INVALID_QUANTITY,
    LEVEL_EMPTY,
    EJECT_FAILED,
    LOAD_FAILED,
    DIAG_ALL_BLOCKED,
    DIAG_OK,
    DIAG_RETRACT_OK,
    DIAG_RETRACT_FAILED
};

class TreatDispenser {
public:
    TreatDispenser();
    ~TreatDispenser();

    bool init();
    DispenseResult dispense(int speed, int quantity);
    void cleanup();

private:
    // Helper control functions
    bool eject_treat(int speed);
    bool load_new_treat();
    void retract_sorter(int steps);
    void set_thrower_motor_speed(int speed_level, bool forward);
    void stop_thrower_motor();
    void step_sorter_once(bool forward);
    
    // IR readings
    bool get_level_ir();
    bool get_sorter_ir();
    bool get_thrower_ir();
    
    // Safety release
    void disable_sorter_coils();

    // libgpiod resources
    gpiod_chip* chip_;
    
    // Stepper motor lines
    gpiod_line* stepper_a_;
    gpiod_line* stepper_b_;
    gpiod_line* stepper_c_;
    gpiod_line* stepper_d_;
    
    // DC motor lines
    gpiod_line* dc_ain1_;
    gpiod_line* dc_ain2_;
    gpiod_line* dc_pwma_;
    gpiod_line* dc_stby_;
    
    // IR lines
    gpiod_line* ir_level_;
    gpiod_line* ir_sorter_;
    gpiod_line* ir_thrower_;

    bool initialized_;
    int stepper_step_number_;
    bool had_loaded_treat_;

    // Software PWM thread variables
    std::atomic<bool> sw_pwm_running_;
    std::thread sw_pwm_thread_;
    std::atomic<int> sw_pwm_duty_;

    // Configurable parameters inside the header file (Subject to testing)
    static constexpr int MAX_LOAD_ATTEMPTS = 3;
    static constexpr int STEPPER_STEP_DELAY_US = 3000;   // Delay between stepper steps (3ms)
    static constexpr int RETRACT_STEPS_BRIEF = 1500;     // Corresponds to 4500ms brief recovery on ESP32
    static constexpr int RETRACT_STEPS_RECOVER = 160;    // Corresponds to 500ms brief recovery on ESP32
    static constexpr int RETRACT_STEPS_FAIL = 1000;      // Corresponds to 3000ms failure retract on ESP32
    static constexpr int LOAD_TIMEOUT_SEC = 10;          // Loading a treat timeout
    static constexpr int EJECT_TIMEOUT_SEC = 5;          // Throwing a treat timeout
};

} // namespace oro

#endif // TREAT_DISPENSER_HPP
