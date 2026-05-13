#pragma once

#include <string>
#include <memory>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include "common/zmq/zmq_publisher.h"
#include "common/config/config_parser.h"

namespace oro {
namespace media {
namespace video {

class VideoPipeline {
public:
    VideoPipeline(std::shared_ptr<zmq_ipc::ZmqPublisher> publisher, const config::VideoConfig& config = config::VideoConfig{});
    ~VideoPipeline();

    bool init();
    void start();
    void stop();
    void set_source_fd(int fd);

private:
    static GstFlowReturn on_new_sample(GstElement* sink, gpointer data);

    std::shared_ptr<zmq_ipc::ZmqPublisher> publisher_;
    config::VideoConfig config_;
    GstElement* pipeline_;
    GstElement* appsink_;
    uint64_t frame_count_;
    int source_fd_;
};

} // namespace video
} // namespace media
} // namespace oro
