#include "common/logging/logger.h"
#include "common/zmq/zmq_publisher.h"
#include "common/config/config_parser.h"
#include "media/video_pipeline.h"
#include "media/cam_splitter_node.h"
#include <memory>
#include <csignal>
#include <thread>
#include <chrono>

bool g_running = true;
std::atomic<bool> g_privacy_mode{false};
std::atomic<bool> g_privacy_changed{false};

void signal_handler(int signum) {
    spdlog::info("Interrupt signal ({}) received.", signum);
    g_running = false;
}

void sigusr1_handler(int) {
    g_privacy_mode = true;
    g_privacy_changed = true;
}

void sigusr2_handler(int) {
    g_privacy_mode = false;
    g_privacy_changed = true;
}

int main() {
    oro::media::logging::init_logger("video_ingestor");
    spdlog::info("Starting Video Ingestor Service...");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGUSR1, sigusr1_handler);
    std::signal(SIGUSR2, sigusr2_handler);

    try {
        oro::media::config::VideoConfig v_config;
        try {
            auto config = oro::media::config::MediaConfig::load("../configs/media_config.json");
            v_config = config.video;
            spdlog::info("Loaded configuration for source device: {}", v_config.source_device);
            spdlog::info("Loaded configuration for CV sink device: {}", v_config.cv_device);
            spdlog::info("Loaded configuration for video call sink device: {}", v_config.videocall_device);
            spdlog::info("Loaded configuration for buffer sink device: {}", v_config.buffer_device);
            spdlog::info("Video pipeline will use device: {}", v_config.device);
        } catch(const std::exception& e) {
            spdlog::warn("Config load failed, using defaults. Error: {}", e.what());
        }

        oro::media::video::CamSplitterNode splitter(
            v_config.source_device,
            v_config.cv_device,
            v_config.videocall_device,
            v_config.buffer_device,
            v_config.width,
            v_config.height,
            v_config.framerate_num,
            v_config.sink_pixelformat
        );

        if (!splitter.start()) {
            spdlog::error("Failed to start camera splitter.");
            return 1;
        }

        auto publisher = std::make_shared<oro::media::zmq_ipc::ZmqPublisher>(v_config.zmq_endpoint, v_config.hwm);
        oro::media::video::VideoPipeline pipeline(publisher, v_config);
        pipeline.set_source_fd(splitter.get_fd());

        if (!pipeline.init()) {
            spdlog::error("Failed to initialize video pipeline.");
            return 1;
        }

        pipeline.start();

        while (g_running) {
            if (g_privacy_changed.exchange(false)) {
                if (g_privacy_mode) {
                    spdlog::info("Privacy Mode Enabled: suspending capture and pipeline.");
                    pipeline.set_privacy_mode(true);
                    splitter.set_privacy_mode(true);
                } else {
                    spdlog::info("Privacy Mode Disabled: resuming capture and pipeline.");
                    splitter.set_privacy_mode(false);
                    pipeline.set_privacy_mode(false);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        pipeline.stop();
        splitter.stop();
        spdlog::info("Video Ingestor Service shut down cleanly.");

    } catch (const std::exception& e) {
        spdlog::error("Exception in Video Ingestor: {}", e.what());
        return 1;
    }

    return 0;
}