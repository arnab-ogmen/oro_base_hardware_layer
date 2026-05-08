#ifndef RADXA_DRIVERS_NODE_HPP
#define RADXA_DRIVERS_NODE_HPP

#include <zmq.hpp>
#include <atomic>
#include <string>
#include <vector>
#include <random>
#include "data/sensor_payloads.hpp"

namespace oro {

class RadxaDriversNode {
public:
    RadxaDriversNode(zmq::socket_t& sensor_pub);
    ~RadxaDriversNode();

    void start();
    void stop();
    void spin_once();

private:
    void publish_treat_ir(uint64_t current_ms);
    void publish_thermal_array(uint64_t current_ms);
    uint64_t now_ms() const;

    zmq::socket_t& sensor_pub_;
    std::atomic<bool> running_{false};

    // Simulation state
    std::mt19937 rng_;
    
    // Treat IR states
    bool ir_level_state_ = false;
    bool ir_sorter_state_ = false;
    bool ir_thrower_state_ = false;
    uint64_t last_ir_update_ms_ = 0;

    // Thermal simulation state
    float heat_source_pos_x_ = 4.0f;
    float heat_source_pos_y_ = 4.0f;
    float heat_source_vel_x_ = 0.1f;
    float heat_source_vel_y_ = 0.05f;
    uint64_t last_thermal_update_ms_ = 0;

    // Rolling sequence numbers (4-bit, 0–15)
    uint8_t thermal_seq_ = 0;
    uint8_t ir_level_seq_ = 0;
    uint8_t ir_sorter_seq_ = 0;
    uint8_t ir_thrower_seq_ = 0;
};

} // namespace oro

#endif // RADXA_DRIVERS_NODE_HPP
