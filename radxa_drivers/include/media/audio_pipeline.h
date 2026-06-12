#pragma once

#include <string>
#include <memory>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include "common/zmq/zmq_publisher.h"
#include "common/config/config_parser.h"

namespace oro {
namespace media {
namespace audio {

class AudioPipeline {
public:
    AudioPipeline(std::shared_ptr<zmq_ipc::ZmqPublisher> publisher, const config::AudioConfig& config);
    ~AudioPipeline();

    bool init();
    void start();
    void stop();
    void set_privacy_mode(bool enable);

private:
    static GstFlowReturn on_new_sample(GstElement* sink, gpointer data);

    std::shared_ptr<zmq_ipc::ZmqPublisher> publisher_;
    config::AudioConfig config_;
    GstElement* pipeline_;
    GstElement* appsink_;
};

} // namespace audio
} // namespace media
} // namespace oro
