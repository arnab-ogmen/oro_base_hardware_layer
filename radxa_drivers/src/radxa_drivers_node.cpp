#include "radxa_drivers_node.hpp"
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
  std::cout << "[RadxaDriversNode] Started sensor simulation" << std::endl;
}

void RadxaDriversNode::stop() { running_ = false; }

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
    const auto& desc = TOPIC_REGISTRY[TID_THERMAL_ARRAY];
    ThermalPayload payload{};
    payload.header.sensor_id = TID_THERMAL_ARRAY;
    payload.header.seq_num = thermal_seq_;
    payload.header.timestamp_ms = current_ms;

    thermal_seq_ = (thermal_seq_ + 1) & 0x0F; // 4-bit rolling sequence

  // Simulate Physics-based heat blob
  // Move heat source
  heat_source_pos_x_ += heat_source_vel_x_;
  heat_source_pos_y_ += heat_source_vel_y_;

  // Bounce off walls (8x8 grid)
  if (heat_source_pos_x_ < 0.5f || heat_source_pos_x_ > 6.5f)
    heat_source_vel_x_ *= -1.0f;
  if (heat_source_pos_y_ < 0.5f || heat_source_pos_y_ > 6.5f)
    heat_source_vel_y_ *= -1.0f;

  float ambient =
      22.5f + (std::uniform_real_distribution<float>(-0.1f, 0.1f)(rng_));
  float blob_temp = 36.5f; // Human body temp

  payload.frame.timestamp_ms = static_cast<uint32_t>(current_ms);
  payload.frame.ambient_temp = ambient;
  payload.frame.overflow = 0;

  float current_min = 100.0f;
  float current_max = -100.0f;

  // Fill 8x8 array
  for (int r = 0; r < 8; ++r) {
    for (int c = 0; c < 8; ++c) {
      float dx = c - heat_source_pos_x_;
      float dy = r - heat_source_pos_y_;
      float dist_sq = dx * dx + dy * dy;

      // Gaussian kernel for heat blob
      float heat = (blob_temp - ambient) * std::exp(-dist_sq / 2.0f);
      float val = ambient + heat;

      // Add some sensor noise
      val += std::uniform_real_distribution<float>(-0.2f, 0.2f)(rng_);

      payload.frame.pixels[r * 8 + c] = val;
      if (val < current_min)
        current_min = val;
      if (val > current_max)
        current_max = val;
    }
  }

  payload.frame.min_temp = current_min;
  payload.frame.max_temp = current_max;

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
