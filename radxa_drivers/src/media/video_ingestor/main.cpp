#include "common/logging/logger.h"
#include "common/zmq/zmq_publisher.h"
#include "common/config/config_parser.h"
#include "media/video_pipeline.h"
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
    oro::media::logging::init_logger("video_ingestor");
    spdlog::info("Starting Video Ingestor Service...");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        oro::media::config::VideoConfig v_config;
        try {
            auto config = oro::media::config::MediaConfig::load("../configs/media_config.json");
            v_config = config.video;
            spdlog::info("Loaded configuration for video device: {}", v_config.device);
        } catch(const std::exception& e) {
            spdlog::warn("Config load failed, using defaults. Error: {}", e.what());
        }

        auto publisher = std::make_shared<oro::media::zmq_ipc::ZmqPublisher>(v_config.zmq_endpoint, v_config.hwm);
        oro::media::video::VideoPipeline pipeline(publisher, v_config);

        if (!pipeline.init()) {
            spdlog::error("Failed to initialize video pipeline.");
            return 1;
        }

        pipeline.start();

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        pipeline.stop();
        spdlog::info("Video Ingestor Service shut down cleanly.");

    } catch (const std::exception& e) {
        spdlog::error("Exception in Video Ingestor: {}", e.what());
        return 1;
    }

    return 0;
}
