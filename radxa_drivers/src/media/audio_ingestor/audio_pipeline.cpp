#include "media/audio_pipeline.h"
#include "common/timing/monotonic_clock.h"
#include "audio_frame_generated.h" // FlatBuffers generated header
#include <spdlog/spdlog.h>
#include <flatbuffers/flatbuffers.h>

namespace oro {
namespace media {
namespace audio {

AudioPipeline::AudioPipeline(std::shared_ptr<zmq_ipc::ZmqPublisher> publisher, const config::AudioConfig& config)
    : publisher_(publisher), config_(config), pipeline_(nullptr), appsink_(nullptr) {
    gst_init(nullptr, nullptr);
}

AudioPipeline::~AudioPipeline() {
    stop();
    if (pipeline_) {
        gst_object_unref(pipeline_);
    }
}

bool AudioPipeline::init() {
    GError* error = nullptr;
    // We use a robust GStreamer pipeline string based on architecture specs
    std::string pipeline_str = 
        "alsasrc device=" + config_.device + " ! "
        "audioconvert ! audioresample ! "
        "audio/x-raw,rate=" + std::to_string(config_.rate) +
        ",channels=" + std::to_string(config_.channels) +
        ",format=" + config_.format + " ! "
        "queue ! "
        "appsink name=sink emit-signals=true max-buffers=5 drop=true";

    pipeline_ = gst_parse_launch(pipeline_str.c_str(), &error);

    if (error) {
        spdlog::error("Failed to parse audio pipeline: {}", error->message);
        g_error_free(error);
        return false;
    }

    appsink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
    if (!appsink_) {
        spdlog::error("Failed to get appsink element");
        return false;
    }

    g_signal_connect(appsink_, "new-sample", G_CALLBACK(on_new_sample), this);

    spdlog::info("Audio pipeline initialized successfully.");
    return true;
}

void AudioPipeline::start() {
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_PLAYING);
        spdlog::info("Audio pipeline started.");
    }
}

void AudioPipeline::stop() {
    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        spdlog::info("Audio pipeline stopped.");
    }
}

GstFlowReturn AudioPipeline::on_new_sample(GstElement* sink, gpointer data) {
    AudioPipeline* self = static_cast<AudioPipeline*>(data);
    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    
    if (sample) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        GstCaps* caps = gst_sample_get_caps(sample);
        
        // Extract basic metadata
        GstStructure* s = gst_caps_get_structure(caps, 0);
        int rate = 0, channels = 0;
        gst_structure_get_int(s, "rate", &rate);
        gst_structure_get_int(s, "channels", &channels);

        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            // Get Monotonic RAW time for correlation
            uint64_t monotonic_ns = timing::get_monotonic_raw_ns();
            
            flatbuffers::FlatBufferBuilder builder(map.size + 1024);
            auto fb_samples = builder.CreateVector(map.data, map.size);

            AudioFrameBuilder frame_builder(builder);
            frame_builder.add_timestamp_ns(monotonic_ns);
            frame_builder.add_sample_rate(rate);
            frame_builder.add_channels(channels);
            frame_builder.add_samples(fb_samples);
            
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

} // namespace audio
} // namespace media
} // namespace oro
