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
    // Physical source
    std::string source_device   = "/dev/video0";

    // v4l2loopback virtual camera sinks
    std::string cv_device         = "/dev/video11";  ///< VCam1 — CV pipeline
    std::string videocall_device  = "/dev/video12";  ///< VCam2 — video call
    std::string buffer_device     = "/dev/video13";  ///< VCam3 — buffer / TBD

    // Device used by the GStreamer ingestor pipeline (typically == cv_device)
    std::string device            = "/dev/video11";

    // Capture & output format
    int         width             = 1280;
    int         height            = 720;
    int         framerate_num     = 30;
    int         framerate_den     = 1;

    /// Pixel-format written to v4l2loopback sinks via VIDIOC_S_FMT.
    /// Supported values: "MJPG" (default), "YUYV".
    /// Must match the format expected by downstream consumers.
    std::string sink_pixelformat  = "MJPG";

    // ZMQ transport
    std::string zmq_endpoint      = "ipc:///tmp/video.sock";
    int         hwm               = 3;
};

struct AudioConfig {
    std::string device = "hw:1,0";
    int rate = 16000;
    int channels = 1;
    std::string format = "S16LE";
    std::string zmq_endpoint = "ipc:///tmp/audio.sock";
    int hwm = 3;
};

struct AVConfig {
    std::string zmq_endpoint = "ipc:///tmp/av.sock";
    int hwm = 3;
    std::string audio_playback_device = "plughw:1,0";
};

struct MediaConfig {
    VideoConfig video;
    AudioConfig audio;
    AVConfig av;

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
            config.video.source_device    = v.value("source_device",    "/dev/video0");
            config.video.cv_device        = v.value("cv_device",        "/dev/video11");
            config.video.videocall_device = v.value("videocall_device", "/dev/video12");
            config.video.buffer_device    = v.value("buffer_device",    "/dev/video13");
            config.video.device           = v.value("device",           config.video.cv_device);
            config.video.width            = v.value("width",            1280);
            config.video.height           = v.value("height",           720);
            config.video.framerate_num    = v.value("framerate_num",    30);
            config.video.framerate_den    = v.value("framerate_den",    1);
            config.video.sink_pixelformat = v.value("sink_pixelformat", "MJPG");
            config.video.zmq_endpoint     = v.value("zmq_endpoint",     "ipc:///tmp/video.sock");
            config.video.hwm              = v.value("hwm",              3);
        }

        // Load Audio
        if (j.contains("audio")) {
            auto& a = j["audio"];
            config.audio.device = a.value("device", "hw:1,0");
            config.audio.rate = a.value("rate", 16000);
            config.audio.channels = a.value("channels", 1);
            config.audio.format = a.value("format", "S16LE");
            config.audio.zmq_endpoint = a.value("zmq_endpoint", "ipc:///tmp/audio.sock");
            config.audio.hwm = a.value("hwm", 3);
        }

        // Load AV
        if (j.contains("av")) {
            auto& av = j["av"];
            config.av.zmq_endpoint          = av.value("zmq_endpoint", "ipc:///tmp/av.sock");
            config.av.hwm                   = av.value("hwm", 3);
            config.av.audio_playback_device = av.value("audio_playback_device", "plughw:1,0");
        }

        return config;
    }
};

} // namespace config
} // namespace media
} // namespace oro