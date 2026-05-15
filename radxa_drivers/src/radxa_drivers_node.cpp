#include "radxa_drivers/radxa_drivers_node.hpp"
#include "radxa_drivers/drivers/amg8833_regs.h"
#include "data/topic_registry.hpp"
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>

namespace oro {

RadxaDriversNode::RadxaDriversNode(zmq::socket_t &sensor_pub)
    : sensor_pub_(sensor_pub), rng_(std::random_device{}()) {
  std::cout << "[RadxaDriversNode] Initialized with shared sensor publisher"
            << std::endl;
}

RadxaDriversNode::~RadxaDriversNode() { stop(); }

void RadxaDriversNode::start() {
  running_ = true;
  last_ir_update_ms_ = now_ms();
  last_thermal_update_ms_ = last_ir_update_ms_;

  // Initialize AMG8833
  amg_config_t cfg;
  cfg.i2c_dev = "/dev/i2c-7";
  cfg.i2c_addr = AMG_ADDR_HIGH;
  cfg.fps = AMG_FPS_10;
  cfg.hw_avg = AMG_AVG_NONE;
  cfg.ema_alpha = 0.3f;

  if (amg_init(&cfg, &amg_handle_) != AMG_OK) {
    std::cerr << "[RadxaDriversNode] Failed to initialize AMG8833 thermal camera"
              << std::endl;
    amg_handle_ = nullptr;
  } else {
    std::cout << "[RadxaDriversNode] AMG8833 thermal camera initialized"
              << std::endl;
  }

  std::cout << "[RadxaDriversNode] Started sensor drivers" << std::endl;
}

void RadxaDriversNode::stop() {
  running_ = false;
  if (amg_handle_) {
    amg_close(amg_handle_);
    amg_handle_ = nullptr;
  }
}

void RadxaDriversNode::spin_once() {
  if (!running_.load())
    return;

  uint64_t current_ms = now_ms();

  // 1. Publish Treat IR Sensors (1Hz simulation logic)
  if (current_ms - last_ir_update_ms_ >= 1000) {
    publish_treat_ir(current_ms);
    last_ir_update_ms_ = current_ms;
  }

  // 2. Publish Thermal Array (10Hz / 100ms interval)
  if (current_ms - last_thermal_update_ms_ >= 100) {
    publish_thermal_array(current_ms);
    last_thermal_update_ms_ = current_ms;
  }
}

void RadxaDriversNode::publish_treat_ir(uint64_t current_ms) {
  std::uniform_real_distribution<float> dist(0.0, 1.0);

  // Simulate rare triggers
  if (dist(rng_) > 0.95)
    ir_level_state_ = !ir_level_state_;
  if (dist(rng_) > 0.90)
    ir_sorter_state_ = !ir_sorter_state_;
  if (dist(rng_) > 0.85)
    ir_thrower_state_ = !ir_thrower_state_;

  auto send_digital = [&](uint8_t tid, bool state, uint8_t &seq) {
    const auto &desc = TOPIC_REGISTRY[tid];
    DigitalPayload payload{};
    payload.header.sensor_id = tid; // Use Topic ID for host-side sensors
        payload.header.seq_num = seq;
        payload.header.timestamp_ms = current_ms;
        payload.state = state ? 1 : 0;

        seq = (seq + 1) & 0x0F; // 4-bit rolling sequence

        sensor_pub_.send(zmq::const_buffer(desc.zmq_topic, std::strlen(desc.zmq_topic)), zmq::send_flags::sndmore);
        sensor_pub_.send(zmq::const_buffer(&payload, sizeof(payload)), zmq::send_flags::none);
    };

    send_digital(TID_TREAT_LEVEL_IR, ir_level_state_, ir_level_seq_);
    send_digital(TID_TREAT_SORTER_IR, ir_sorter_state_, ir_sorter_seq_);
    send_digital(TID_TREAT_THROWER_IR, ir_thrower_state_, ir_thrower_seq_);
}

void RadxaDriversNode::publish_thermal_array(uint64_t current_ms) {
  if (!amg_handle_) {
    return;
  }

  ::amg_frame_t frame;
  if (amg_read_frame(amg_handle_, &frame) != AMG_OK) {
    return;
  }

  const auto &desc = TOPIC_REGISTRY[TID_THERMAL_ARRAY];
  ThermalPayload payload{};
  payload.header.sensor_id = TID_THERMAL_ARRAY;
  payload.header.seq_num = thermal_seq_;
  payload.header.timestamp_ms = current_ms;

  thermal_seq_ = (thermal_seq_ + 1) & 0x0F; // 4-bit rolling sequence

  payload.frame.timestamp_ms = frame.timestamp_ms;
  payload.frame.ambient_temp = frame.ambient_temp;
  payload.frame.min_temp = frame.min_temp;
  payload.frame.max_temp = frame.max_temp;
  payload.frame.overflow = frame.overflow;
  std::memcpy(payload.frame.pixels, frame.pixels, sizeof(frame.pixels));

  // Send multi-part
  sensor_pub_.send(
      zmq::const_buffer(desc.zmq_topic, std::strlen(desc.zmq_topic)),
      zmq::send_flags::sndmore);
  sensor_pub_.send(zmq::const_buffer(&payload, sizeof(payload)),
                   zmq::send_flags::none);
}

uint64_t RadxaDriversNode::now_ms() const {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

} // namespace oro
