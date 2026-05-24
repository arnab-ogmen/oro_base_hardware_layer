#ifndef GPIO_SENSOR_HPP
#define GPIO_SENSOR_HPP

#include <string>
#include <gpiod.h>

class GPIOSensor {
public:
    enum class Bias {
        NONE,
        PULL_UP,
        PULL_DOWN
    };

    enum class Edge {
        RISING,
        FALLING,
        BOTH
    };

    // Constructor. Takes chip name (e.g. "gpiochip0") and line offset.
    GPIOSensor(const std::string& chip_name, int line_num, Bias bias = Bias::PULL_DOWN, const std::string& consumer = "gpio_sensor");
    ~GPIOSensor();

    // Prevent copy
    GPIOSensor(const GPIOSensor&) = delete;
    GPIOSensor& operator=(const GPIOSensor&) = delete;

    // Read current value (0 or 1)
    int read();

    // Enable event monitoring with specified edge trigger
    bool enableEvents(Edge edge);

    // Wait for event with timeout in milliseconds.
    // Returns 1 if event occurred, 0 on timeout, -1 on error.
    // sets is_rising to true if rising edge, false otherwise.
    int waitForEvent(int timeout_ms, bool& is_rising);

private:
    std::string chip_name;
    int line_num;
    gpiod_chip* chip;
    gpiod_line* line;
    bool events_enabled;
    Bias bias;
    std::string consumer;

    bool requestInput();
};

#endif // GPIO_SENSOR_HPP
