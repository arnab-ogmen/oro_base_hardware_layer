#ifndef CAMHEAD_ROTATION_HPP
#define CAMHEAD_ROTATION_HPP

#include <atomic>
#include <cstdint>
#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

class GPIOSensor;
class Stepper;

namespace oro {

class CamHeadRotationNode {
public:
    CamHeadRotationNode();
    ~CamHeadRotationNode();
    
    // launch Stepper background thread
    void start();

    // Signal threads to stop and join them
    void stop();
    
    // Expose control of stepper target angle
    void set_stepper_target_angle(float target_angle);

    // Methods to read/update state (to be called by external nodes/threads for publishing)
    bool update_limit_switch1(uint64_t current_ms, int& out_val);
    bool update_limit_switch2(uint64_t current_ms, int& out_val);
    bool update_home_sensor(uint64_t current_ms, int& out_val);
    bool update_stepper_status(int& out_running);
    bool update_stepper_encoder(int32_t& out_ticks);

    // Helpers to query current values directly
    int get_limit_switch1_value() const { return limit_switch1_debounced_val_; }
    int get_limit_switch2_value() const { return limit_switch2_debounced_val_; }
    int get_home_sensor_value() const { return home_sensor_debounced_val_; }
    bool is_stepper_running() const { return stepper_running_.load(); }
    float get_stepper_current_angle() const { return stepper_current_angle_.load(); }
    bool is_stepper_home_calibrated() const { return stepper_home_calibrated_.load(); }

private:
    // Limit Switches (wired directly to Radxa GPIOs)
    std::unique_ptr<GPIOSensor> limit_switch1_;
    std::unique_ptr<GPIOSensor> limit_switch2_;

    // Limit Switches debouncing state (instantaneous with 50ms lockout)
    int limit_switch1_debounced_val_ = -1;
    uint64_t limit_switch1_lockout_until_ms_ = 0;

    int limit_switch2_debounced_val_ = -1;
    uint64_t limit_switch2_lockout_until_ms_ = 0;

    // Home Sensor (wired directly to Radxa GPIOs)
    std::unique_ptr<GPIOSensor> home_sensor_;

    // Home Sensor debouncing state (instantaneous with 50ms lockout)
    int home_sensor_debounced_val_ = -1;
    uint64_t home_sensor_lockout_until_ms_ = 0;

    // Stepper Motor (wired directly to Radxa GPIOs)
    std::unique_ptr<Stepper> stepper_;
    std::thread stepper_thread_;
    std::mutex stepper_mtx_;
    std::condition_variable stepper_cv_;

    static constexpr float TOTAL_STEPS = 6100.0f;
    
    std::atomic<float> stepper_current_angle_{0.0f};
    std::atomic<float> stepper_target_angle_{0.0f};
    std::atomic<bool> stepper_target_updated_{false};
    std::atomic<bool> stepper_running_{false};
    std::atomic<bool> stepper_home_calibrated_{false};
    std::atomic<bool> running_{false};

    // Helper methods for homing/calibration
    bool is_home_sensor_active();
    bool is_left_limit_switch_pressed();
    bool is_right_limit_switch_pressed();
    
    void settle_home_edge(int move_dir);
    void seek_cam_head_home_internal();
    void seek_cam_head_home();
    void stepper_thread_func();
};

} // namespace oro

#endif // CAMHEAD_ROTATION_HPP