#include "media/video_pipeline.h"
#include "common/timing/monotonic_clock.h"
#include "video_frame_generated.h" // FlatBuffers generated header
#include <spdlog/spdlog.h>
#include <flatbuffers/flatbuffers.h>

namespace oro {
namespace media {
namespace video {

VideoPipeline::VideoPipeline(std::shared_ptr<zmq_ipc::ZmqPublisher> publisher, const config::VideoConfig& config)
    : publisher_(publisher), config_(config), pipeline_(nullptr), appsink_(nullptr), frame_count_(0) {
    gst_init(nullptr, nullptr);
}

VideoPipeline::~VideoPipeline() {
    stop();
    if (pipeline_) {
        gst_object_unref(pipeline_);
    }
}

bool VideoPipeline::init() {
    GError* error = nullptr;
    // We use a robust GStreamer pipeline string based on architecture specs
    // Note: Swapped mppjpegdec -> jpegdec for local x86 testing. Revert on RK3588!
    std::string pipeline_str = 
        "v4l2src device=" + config_.device + " io-mode=dmabuf ! "
        "image/jpeg,width=" + std::to_string(config_.width) + ",height=" + std::to_string(config_.height) +
        ",framerate=" + std::to_string(config_.framerate_num) + "/" + std::to_string(config_.framerate_den) + " ! "
        "jpegparse ! "
        // "mppjpegdec ! "
        "jpegdec ! "
        "videoconvert ! "
        "video/x-raw,format=NV12 ! "
        "appsink name=sink emit-signals=true max-buffers=2 drop=true";

    pipeline_ = gst_parse_launch(pipeline_str.c_str(), &error);

    if (error) {
        spdlog::error("Failed to parse video pipeline: {}", error->message);
        g_error_free(error);
        return false;
    }

    appsink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
    if (!appsink_) {
        spdlog::error("Failed to get appsink element");
        return false;
    }

    g_signal_connect(appsink_, "new-sample", G_CALLBACK(on_new_sample), this);

    spdlog::info("Video pipeline initialized successfully.");
    return true;
}

void VideoPipeline::start() {
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);
        spdlog::info("Video pipeline started.");
    }
}

void VideoPipeline::stop() {
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        spdlog::info("Video pipeline stopped.");
    }
}

GstFlowReturn VideoPipeline::on_new_sample(GstElement* sink, gpointer data) {
    VideoPipeline* self = static_cast<VideoPipeline*>(data);
    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    
    if (sample) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstCaps* caps = gst_sample_get_caps(sample);
        
        // Extract basic metadata
        GstStructure* s = gst_caps_get_structure(caps, 0);
        int width = 0, height = 0;
        gst_structure_get_int(s, "width", &width);
        gst_structure_get_int(s, "height", &height);

        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            // Get Monotonic RAW time for correlation
            uint64_t monotonic_ns = timing::get_monotonic_raw_ns();
            
            flatbuffers::FlatBufferBuilder builder(map.size + 1024);
            
            auto fb_format = builder.CreateString("NV12");
            auto fb_data = builder.CreateVector(map.data, map.size);

            VideoFrameBuilder frame_builder(builder);
            frame_builder.add_timestamp_ns(monotonic_ns);
            frame_builder.add_width(width);
            frame_builder.add_height(height);
            frame_builder.add_format(fb_format);
            frame_builder.add_frame_id(self->frame_count_++);
            frame_builder.add_data(fb_data);
            
            auto frame = frame_builder.Finish();
            builder.Finish(frame);

            self->publisher_->publish(builder.GetBufferPointer(), builder.GetSize());
            
            gst_buffer_unmap(buffer, &map);
        }
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }
    return GST_FLOW_ERROR;
}

} // namespace video
} // namespace media
} // namespace oro
