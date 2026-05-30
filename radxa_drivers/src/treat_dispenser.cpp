#include "radxa_drivers/treat_dispenser.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <stdexcept>

namespace oro {

// Dual-phase energized custom sequence for 4-phase stepper motor
static const int USER_FULLSTEP_SEQ[4][4] = {
    {1, 0, 1, 0}, // Step 0: A+C
    {0, 1, 1, 0}, // Step 1: B+C
    {0, 1, 0, 1}, // Step 2: B+D
    {1, 0, 0, 1}  // Step 3: A+D
};

TreatDispenser::TreatDispenser()
    : chip_(nullptr),
      stepper_a_(nullptr), stepper_b_(nullptr), stepper_c_(nullptr), stepper_d_(nullptr),
      dc_ain1_(nullptr), dc_ain2_(nullptr), dc_pwma_(nullptr), dc_stby_(nullptr),
      ir_level_(nullptr), ir_sorter_(nullptr), ir_thrower_(nullptr),
      initialized_(false), stepper_step_number_(0), had_loaded_treat_(false),
      sw_pwm_running_(false), sw_pwm_duty_(30) {}

TreatDispenser::~TreatDispenser() {
    cleanup();
}

bool TreatDispenser::init() {
    if (initialized_) return true;

    std::cout << "[TreatDispenser] Connecting to gpiochip0..." << std::endl;
    chip_ = gpiod_chip_open_by_name("gpiochip0");
    if (!chip_) {
        std::cerr << "[TreatDispenser] ERROR: Failed to open gpiochip0" << std::endl;
        return false;
    }

    // Get line objects by offsets
    stepper_a_ = gpiod_chip_get_line(chip_, 32); // Pin 7
    stepper_b_ = gpiod_chip_get_line(chip_, 33); // Pin 11
    stepper_c_ = gpiod_chip_get_line(chip_, 34); // Pin 29
    stepper_d_ = gpiod_chip_get_line(chip_, 35); // Pin 31

    dc_ain1_ = gpiod_chip_get_line(chip_, 109);  // Pin 21
    dc_ain2_ = gpiod_chip_get_line(chip_, 108);  // Pin 19
    dc_pwma_ = gpiod_chip_get_line(chip_, 107);  // Pin 23
    dc_stby_ = gpiod_chip_get_line(chip_, 37);   // Pin 12

    ir_level_   = gpiod_chip_get_line(chip_, 41); // Pin 8
    ir_sorter_  = gpiod_chip_get_line(chip_, 40); // Pin 38
    ir_thrower_ = gpiod_chip_get_line(chip_, 42); // Pin 10

    if (!stepper_a_ || !stepper_b_ || !stepper_c_ || !stepper_d_ ||
        !dc_ain1_ || !dc_ain2_ || !dc_pwma_ || !dc_stby_ ||
        !ir_level_ || !ir_sorter_ || !ir_thrower_) {
        std::cerr << "[TreatDispenser] ERROR: Failed to get some GPIO line offsets" << std::endl;
        cleanup();
        return false;
    }

    // Setup Outputs (Stepper phases & DC H-Bridge controls)
    if (gpiod_line_request_output(stepper_a_, "treat_dispenser", 0) < 0 ||
        gpiod_line_request_output(stepper_b_, "treat_dispenser", 0) < 0 ||
        gpiod_line_request_output(stepper_c_, "treat_dispenser", 0) < 0 ||
        gpiod_line_request_output(stepper_d_, "treat_dispenser", 0) < 0 ||
        gpiod_line_request_output(dc_ain1_, "treat_dispenser", 0) < 0 ||
        gpiod_line_request_output(dc_ain2_, "treat_dispenser", 0) < 0 ||
        gpiod_line_request_output(dc_pwma_, "treat_dispenser", 0) < 0 ||
        gpiod_line_request_output(dc_stby_, "treat_dispenser", 0) < 0) {
        std::cerr << "[TreatDispenser] ERROR: Failed to request output lines" << std::endl;
        cleanup();
        return false;
    }

    // Setup Inputs (IR sensors with active Pull-Up)
    gpiod_line_request_config config = {
        .consumer = "treat_dispenser",
        .request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT,
        .flags = GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP
    };

    if (gpiod_line_request(ir_level_, &config, 0) < 0 ||
        gpiod_line_request(ir_sorter_, &config, 0) < 0 ||
        gpiod_line_request(ir_thrower_, &config, 0) < 0) {
        std::cerr << "[TreatDispenser] ERROR: Failed to request input lines with Pull-Up" << std::endl;
        cleanup();
        return false;
    }

    // Set all outputs to initial Low (0)
    gpiod_line_set_value(stepper_a_, 0);
    gpiod_line_set_value(stepper_b_, 0);
    gpiod_line_set_value(stepper_c_, 0);
    gpiod_line_set_value(stepper_d_, 0);
    gpiod_line_set_value(dc_ain1_, 0);
    gpiod_line_set_value(dc_ain2_, 0);
    gpiod_line_set_value(dc_pwma_, 0);
    gpiod_line_set_value(dc_stby_, 0);

    initialized_ = true;
    std::cout << "[TreatDispenser] Flawlessly initialized treat hardware lines!" << std::endl;
    return true;
}

DispenseResult TreatDispenser::dispense(int speed, int quantity) {
    // Safety: ensure any dangling motor/stepper states are completely stopped
    stop_thrower_motor();
    disable_sorter_coils();

    if (!initialized_) {
        std::cerr << "[TreatDispenser] Error: Sorter motor not initialized" << std::endl;
        return DispenseResult::NOT_INITIALIZED;
    }

    // A. Special Case: Diagnostics Check (speed == 0 && quantity == 0)
    if (speed == 0 && quantity == 0) {
        bool level_status = get_level_ir();
        bool sorter_status = get_sorter_ir();
        bool thrower_status = get_thrower_ir();

        if (!level_status && !sorter_status && !thrower_status) {
            return DispenseResult::DIAG_ALL_BLOCKED;
        }
        return DispenseResult::DIAG_OK; // Default diagnostics code
    }

    // B. Special Case: Retract Diagnostics (speed == 5 && quantity == 0)
    if (speed == 5 && quantity == 0) {
        retract_sorter(RETRACT_STEPS_BRIEF);
        bool all_low = (!gpiod_line_get_value(stepper_a_) &&
                        !gpiod_line_get_value(stepper_b_) &&
                        !gpiod_line_get_value(stepper_c_) &&
                        !gpiod_line_get_value(stepper_d_));
        if (all_low) {
            return DispenseResult::DIAG_RETRACT_OK;
        } else {
            return DispenseResult::DIAG_RETRACT_FAILED;
        }
    }

    // C. Verify quantity
    if (quantity <= 0) {
        stop_thrower_motor();
        disable_sorter_coils();
        return DispenseResult::INVALID_QUANTITY; // Treat failed / invalid quantity
    }

    // D. Verify Level IR indicator (true = clear path / empty)
    if (get_level_ir()) {
        stop_thrower_motor();
        disable_sorter_coils();
        return DispenseResult::LEVEL_EMPTY; // Level indicator empty error
    }

    int treats_ejected = 0;
    bool pocket_contains_treat = (!get_thrower_ir()); // 0 = Blocked / Loaded

    // E. Primary Loop: Loop until we have successfully ejected the requested quantity of treats
    while (treats_ejected < quantity) {
        if (pocket_contains_treat) {
            if (!eject_treat(speed)) {
                return DispenseResult::EJECT_FAILED;
            }
            treats_ejected++;
            pocket_contains_treat = false;
        } else {
            // Load a new treat from the hopper
            bool loaded = false;
            for (int attempt = 1; attempt <= MAX_LOAD_ATTEMPTS; ++attempt) {
                if (load_new_treat()) {
                    loaded = true;
                    break;
                }

                // Recovery: briefly retract to clear jam, stop, and backoff
                retract_sorter(RETRACT_STEPS_RECOVER);
                disable_sorter_coils();
                std::this_thread::sleep_for(std::chrono::microseconds(attempt * 200));
            }

            if (!loaded) {
                retract_sorter(RETRACT_STEPS_FAIL);
                disable_sorter_coils();
                return DispenseResult::LOAD_FAILED;
            }

            pocket_contains_treat = true;
        }
    }

    // F. Preload Phase: If the pocket is empty, attempt to load one final treat to keep it pre-loaded
    if (!pocket_contains_treat) {
        for (int attempt = 1; attempt <= MAX_LOAD_ATTEMPTS; ++attempt) {
            if (load_new_treat()) {
                break;
            }
            retract_sorter(RETRACT_STEPS_RECOVER);
            disable_sorter_coils();
            std::this_thread::sleep_for(std::chrono::microseconds(attempt * 200));
        }
    }

    // G. Successful dispense cycle: briefly retract to clear path, de-energize
    retract_sorter(RETRACT_STEPS_BRIEF);
    disable_sorter_coils();
    stop_thrower_motor();

    return DispenseResult::SUCCESS; // Treat Dispense Success
}

bool TreatDispenser::eject_treat(int speed) {
    std::cout << "[TreatDispenser] Ejecting treat at speed level " << speed << "..." << std::endl;
    set_thrower_motor_speed(speed, true);

    auto start_time = std::chrono::steady_clock::now();
    bool ejected = false;

    // Run the DC motor for exactly 2 seconds (2000 milliseconds) to give the treat sufficient momentum to eject
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count() < 2000) {
        if (get_thrower_ir()) { // 1 = Clear
            ejected = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    stop_thrower_motor();
    return ejected;
}

bool TreatDispenser::load_new_treat() {
    std::cout << "[TreatDispenser] Stepping sorter stepper forward in user_full mode..." << std::endl;
    auto start_time = std::chrono::steady_clock::now();
    bool loaded = false;

    // Run until sorter IR transitions (0 = obstacle detected / treat fell into position)
    while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time).count() < LOAD_TIMEOUT_SEC) {
        step_sorter_once(false);
        if (!get_sorter_ir()) { // 0 = Blocked / Loaded
            loaded = true;
            break;
        }
    }

    disable_sorter_coils();
    return loaded;
}

void TreatDispenser::retract_sorter(int steps) {
    std::cout << "[TreatDispenser] Retracting sorter stepper backward by " << steps << " steps..." << std::endl;
    for (int i = 0; i < steps; ++i) {
        step_sorter_once(true);
    }
    disable_sorter_coils();
}

void TreatDispenser::step_sorter_once(bool forward) {
    if (forward) {
        stepper_step_number_--;
    } else {
        stepper_step_number_++;
    }
    int step_idx = (stepper_step_number_ % 4 + 4) % 4;

    gpiod_line_set_value(stepper_a_, USER_FULLSTEP_SEQ[step_idx][0]);
    gpiod_line_set_value(stepper_b_, USER_FULLSTEP_SEQ[step_idx][1]);
    gpiod_line_set_value(stepper_c_, USER_FULLSTEP_SEQ[step_idx][2]);
    gpiod_line_set_value(stepper_d_, USER_FULLSTEP_SEQ[step_idx][3]);

    std::this_thread::sleep_for(std::chrono::microseconds(STEPPER_STEP_DELAY_US));
}

void TreatDispenser::set_thrower_motor_speed(int speed_level, bool forward) {
    // 1. Stop any existing software PWM thread
    sw_pwm_running_ = false;
    if (sw_pwm_thread_.joinable()) {
        sw_pwm_thread_.join();
    }

    if (speed_level <= 0) {
        stop_thrower_motor();
        return;
    }

    // 2. Drive standby enable HIGH
    gpiod_line_set_value(dc_stby_, 1);

    // 3. Configure direction
    if (forward) {
        gpiod_line_set_value(dc_ain1_, 0);
        gpiod_line_set_value(dc_ain2_, 1);
    } else {
        gpiod_line_set_value(dc_ain1_, 1);
        gpiod_line_set_value(dc_ain2_, 0);
    }

    // 4. Determine duty cycle percentage
    int duty = 30; // Default is 30%
    if (speed_level == 1) duty = 5;
    else if (speed_level == 2) duty = 10;
    else if (speed_level == 3) duty = 25;
    else if (speed_level == 4) duty = 50;

    sw_pwm_duty_ = duty;
    sw_pwm_running_ = true;

    // 5. Start highly precise and robust software PWM thread
    sw_pwm_thread_ = std::thread([this]() {
        while (sw_pwm_running_.load()) {
            int current_duty = sw_pwm_duty_.load();
            if (current_duty >= 100) {
                gpiod_line_set_value(dc_pwma_, 1);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else if (current_duty <= 0) {
                gpiod_line_set_value(dc_pwma_, 0);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } else {
                // Period of 10ms (100Hz frequency)
                int high_us = current_duty * 100;
                int low_us = (100 - current_duty) * 100;

                gpiod_line_set_value(dc_pwma_, 1);
                std::this_thread::sleep_for(std::chrono::microseconds(high_us));
                gpiod_line_set_value(dc_pwma_, 0);
                std::this_thread::sleep_for(std::chrono::microseconds(low_us));
            }
        }
    });
}

void TreatDispenser::stop_thrower_motor() {
    sw_pwm_running_ = false;
    if (sw_pwm_thread_.joinable()) {
        sw_pwm_thread_.join();
    }

    if (initialized_) {
        gpiod_line_set_value(dc_pwma_, 0);
        gpiod_line_set_value(dc_ain1_, 0);
        gpiod_line_set_value(dc_ain2_, 0);
        gpiod_line_set_value(dc_stby_, 0);
    }
}

bool TreatDispenser::get_level_ir() {
    return (gpiod_line_get_value(ir_level_) != 0); // 1 = Clear
}

bool TreatDispenser::get_sorter_ir() {
    return (gpiod_line_get_value(ir_sorter_) != 0); // 1 = Clear
}

bool TreatDispenser::get_thrower_ir() {
    return (gpiod_line_get_value(ir_thrower_) != 0); // 1 = Clear
}

void TreatDispenser::disable_sorter_coils() {
    if (initialized_) {
        gpiod_line_set_value(stepper_a_, 0);
        gpiod_line_set_value(stepper_b_, 0);
        gpiod_line_set_value(stepper_c_, 0);
        gpiod_line_set_value(stepper_d_, 0);
    }
}

void TreatDispenser::cleanup() {
    stop_thrower_motor();
    disable_sorter_coils();

    if (stepper_a_) gpiod_line_release(stepper_a_);
    if (stepper_b_) gpiod_line_release(stepper_b_);
    if (stepper_c_) gpiod_line_release(stepper_c_);
    if (stepper_d_) gpiod_line_release(stepper_d_);
    if (dc_ain1_) gpiod_line_release(dc_ain1_);
    if (dc_ain2_) gpiod_line_release(dc_ain2_);
    if (dc_pwma_) gpiod_line_release(dc_pwma_);
    if (dc_stby_) gpiod_line_release(dc_stby_);
    if (ir_level_) gpiod_line_release(ir_level_);
    if (ir_sorter_) gpiod_line_release(ir_sorter_);
    if (ir_thrower_) gpiod_line_release(ir_thrower_);

    stepper_a_ = stepper_b_ = stepper_c_ = stepper_d_ = nullptr;
    dc_ain1_ = dc_ain2_ = dc_pwma_ = dc_stby_ = nullptr;
    ir_level_ = ir_sorter_ = ir_thrower_ = nullptr;

    if (chip_) {
        gpiod_chip_close(chip_);
        chip_ = nullptr;
    }

    initialized_ = false;
}

} // namespace oro
