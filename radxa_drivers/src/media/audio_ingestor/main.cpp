#include "common/logging/logger.h"
#include "common/zmq/zmq_publisher.h"
#include "common/config/config_parser.h"
#include "media/audio_pipeline.h"
#include <memory>
#include <csignal>
#include <thread>
#include <chrono>

bool g_running = true;

void signal_handler(int signum) {
    spdlog::info("Interrupt signal ({}) received.", signum);
    g_running = false;
}

int main() {
    oro::media::logging::init_logger("audio_ingestor");
    spdlog::info("Starting Audio Ingestor Service...");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        oro::media::config::AudioConfig a_config;
        try {
            auto config = oro::media::config::MediaConfig::load("../configs/media_config.json");
            a_config = config.audio;
            spdlog::info("Loaded configuration for audio device: {}", a_config.device);
        } catch(const std::exception& e) {
            spdlog::warn("Config load failed, using defaults. Error: {}", e.what());
        }

        auto publisher = std::make_shared<oro::media::zmq_ipc::ZmqPublisher>(a_config.zmq_endpoint, a_config.hwm);
        oro::media::audio::AudioPipeline pipeline(publisher, a_config);

        if (!pipeline.init()) {
            spdlog::error("Failed to initialize audio pipeline.");
            return 1;
        }

        pipeline.start();

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        pipeline.stop();
        spdlog::info("Audio Ingestor Service shut down cleanly.");

    } catch (const std::exception& e) {
        spdlog::error("Exception in Audio Ingestor: {}", e.what());
        return 1;
    }

    return 0;
}