#pragma once

#include <string>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

namespace oro {
namespace media {
namespace config {

struct VideoConfig {
    std::string source_device = "/dev/video0";
    std::string device = "/dev/video11";
    std::string cv_device = "/dev/video11";
    std::string videocall_device = "/dev/video12";
    std::string buffer_device = "/dev/video13";
    int width = 1280;
    int height = 720;
    int framerate_num = 30;
    int framerate_den = 1;
    std::string zmq_endpoint = "ipc:///tmp/video.sock";
    int hwm = 3;
};

struct AudioConfig {
    std::string device = "hw:2,0";
    int rate = 16000;
    int channels = 1;
    std::string format = "S16LE";
    std::string zmq_endpoint = "ipc:///tmp/audio.sock";
    int hwm = 3;
};

struct MediaConfig {
    VideoConfig video;
    AudioConfig audio;

    static MediaConfig load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open config file: " + path);
        }

        nlohmann::json j;
        file >> j;

        MediaConfig config;

        // Load Video
        if (j.contains("video")) {
            auto& v = j["video"];
            config.video.source_device = v.value("source_device", "/dev/video0");
            config.video.cv_device = v.value("cv_device", "/dev/video11");
            config.video.videocall_device = v.value("videocall_device", "/dev/video12");
            config.video.buffer_device = v.value("buffer_device", "/dev/video13");
            config.video.device = v.value("device", config.video.cv_device);
            config.video.width = v.value("width", 3840);
            config.video.height = v.value("height", 2160);
            config.video.framerate_num = v.value("framerate_num", 30);
            config.video.framerate_den = v.value("framerate_den", 1);
            config.video.zmq_endpoint = v.value("zmq_endpoint", "ipc:///tmp/video.sock");
            config.video.hwm = v.value("hwm", 3);
        }

        // Load Audio
        if (j.contains("audio")) {
            auto& a = j["audio"];
            config.audio.device = a.value("device", "hw:2,0");
            config.audio.rate = a.value("rate", 16000);
            config.audio.channels = a.value("channels", 1);
            config.audio.format = a.value("format", "S16LE");
            config.audio.zmq_endpoint = a.value("zmq_endpoint", "ipc:///tmp/audio.sock");
            config.audio.hwm = a.value("hwm", 3);
        }

        return config;
    }
};

} // namespace config
} // namespace media
} // namespace oro
