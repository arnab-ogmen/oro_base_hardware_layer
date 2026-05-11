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
    std::string device = "/dev/video2";
    int width = 3840;
    int height = 2160;
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
            config.video.device = v.value("device", "/dev/video40");
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
