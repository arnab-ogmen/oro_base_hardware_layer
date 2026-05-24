#include "radxa_drivers/gpio_sensor.hpp"
#include <iostream>
#include <stdexcept>

static gpiod_chip* open_chip(const std::string& name) {
    if (name.empty()) {
        return nullptr;
    }
    if (name.find('/') != std::string::npos) {
        return gpiod_chip_open(name.c_str());
    } else {
        return gpiod_chip_open_by_name(name.c_str());
    }
}

GPIOSensor::GPIOSensor(const std::string& chip_name, int line_num, Bias bias, const std::string& consumer)
    : chip_name(chip_name), line_num(line_num), chip(nullptr), line(nullptr), events_enabled(false), bias(bias), consumer(consumer) {
    
    chip = open_chip(chip_name);
    if (!chip) {
        throw std::runtime_error("Failed to open gpiochip: " + chip_name);
    }

    line = gpiod_chip_get_line(chip, line_num);
    if (!line) {
        gpiod_chip_close(chip);
        chip = nullptr;
        throw std::runtime_error("Failed to get line " + std::to_string(line_num) + " on chip " + chip_name);
    }

    if (!requestInput()) {
        gpiod_chip_close(chip);
        chip = nullptr;
        line = nullptr;
        throw std::runtime_error("Failed to request line " + std::to_string(line_num) + " as input");
    }
}

GPIOSensor::~GPIOSensor() {
    if (line) {
        gpiod_line_release(line);
    }
    if (chip) {
        gpiod_chip_close(chip);
    }
}

bool GPIOSensor::requestInput() {
    gpiod_line_request_config config = {
        .consumer = consumer.c_str(),
        .request_type = GPIOD_LINE_REQUEST_DIRECTION_INPUT,
        .flags = 0
    };

    if (bias == Bias::PULL_UP) {
        config.flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
    } else if (bias == Bias::PULL_DOWN) {
        config.flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN;
    }

    return gpiod_line_request(line, &config, 0) == 0;
}

int GPIOSensor::read() {
    if (!line) return 0;
    return gpiod_line_get_value(line);
}

bool GPIOSensor::enableEvents(Edge edge) {
    if (!line) return false;

    gpiod_line_release(line);
    events_enabled = false;

    gpiod_line_request_config config = {
        .consumer = consumer.c_str(),
        .request_type = GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES,
        .flags = 0
    };

    if (edge == Edge::RISING) {
        config.request_type = GPIOD_LINE_REQUEST_EVENT_RISING_EDGE;
    } else if (edge == Edge::FALLING) {
        config.request_type = GPIOD_LINE_REQUEST_EVENT_FALLING_EDGE;
    } else if (edge == Edge::BOTH) {
        config.request_type = GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES;
    }

    if (bias == Bias::PULL_UP) {
        config.flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
    } else if (bias == Bias::PULL_DOWN) {
        config.flags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN;
    }

    if (gpiod_line_request(line, &config, 0) < 0) {
        // Fallback to basic input request if events request fails
        requestInput();
        return false;
    }

    events_enabled = true;
    return true;
}

int GPIOSensor::waitForEvent(int timeout_ms, bool& is_rising) {
    if (!line || !events_enabled) return -1;

    struct timespec timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_nsec = (timeout_ms % 1000) * 1000000L;

    int wait_res = gpiod_line_event_wait(line, &timeout);
    if (wait_res == 1) {
        gpiod_line_event event;
        if (gpiod_line_event_read(line, &event) < 0) {
            return -1;
        }
        is_rising = (event.event_type == GPIOD_LINE_EVENT_RISING_EDGE);
        return 1;
    } else if (wait_res == 0) {
        return 0; // Timeout
    }
    return -1; // Error
}
