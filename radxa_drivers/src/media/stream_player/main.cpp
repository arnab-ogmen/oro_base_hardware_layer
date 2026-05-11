/**
 * Stream Player - Physical validation of the full middleware architecture.
 * 
 * Data flow validated:
 *   Camera → GStreamer → FlatBuffers → ZMQ PUB →
 *   → ZMQ SUB → FlatBuffers Deserialize → GStreamer appsrc → Display + Speakers
 * 
 * This proves the entire round-trip from hardware capture to playback
 * through our ZeroMQ IPC and FlatBuffers serialization layer.
 */

#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <zmq.hpp>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <spdlog/spdlog.h>
#include "video_frame_generated.h"
#include "audio_frame_generated.h"

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    spdlog::info("Interrupt signal ({}) received. Shutting down...", signum);
    g_running = false;
}

// ─── Video Playback Thread ───────────────────────────────────────────────────
void video_player_thread(const std::string& zmq_endpoint) {
    spdlog::info("[VIDEO Player] Subscribing to {}", zmq_endpoint);

    // ZMQ subscriber
    zmq::context_t ctx(1);
    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.set(zmq::sockopt::rcvtimeo, 500); // 500ms timeout for clean shutdown
    sub.connect(zmq_endpoint);
    sub.set(zmq::sockopt::subscribe, "");

    // GStreamer playback pipeline: appsrc → videoconvert → autovideosink
    gst_init(nullptr, nullptr);

    // We will get the actual width/height from the first frame's FlatBuffer metadata
    GstElement* pipeline = gst_parse_launch(
        "appsrc name=src is-live=true format=time ! "
        "videoconvert ! "
        "queue max-size-buffers=2 ! "
        "autovideosink sync=false",
        nullptr
    );

    GstElement* appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");
    bool caps_set = false;

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    spdlog::info("[VIDEO Player] Pipeline started. Waiting for frames...");

    while (g_running) {
        zmq::message_t msg;
        auto res = sub.recv(msg, zmq::recv_flags::none);
        if (!res) continue; // timeout, check g_running

        auto frame = oro::media::GetVideoFrame(msg.data());
        if (!frame || !frame->data()) continue;

        uint32_t width = frame->width();
        uint32_t height = frame->height();

        // Set caps on first frame (we learn the resolution from the FlatBuffer metadata)
        if (!caps_set) {
            GstCaps* caps = gst_caps_new_simple("video/x-raw",
                "format", G_TYPE_STRING, "NV12",
                "width", G_TYPE_INT, (int)width,
                "height", G_TYPE_INT, (int)height,
                "framerate", GST_TYPE_FRACTION, 30, 1,
                nullptr
            );
            g_object_set(G_OBJECT(appsrc), "caps", caps, nullptr);
            gst_caps_unref(caps);
            caps_set = true;
            spdlog::info("[VIDEO Player] Caps set: {}x{} NV12", width, height);
        }

        // Push the raw pixel data into appsrc
        const uint8_t* data = frame->data()->data();
        size_t size = frame->data()->size();

        GstBuffer* buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
        gst_buffer_fill(buffer, 0, data, size);

        GstFlowReturn ret;
        g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
        gst_buffer_unref(buffer);
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(appsrc);
    gst_object_unref(pipeline);
    spdlog::info("[VIDEO Player] Stopped.");
}

// ─── Audio Playback Thread ───────────────────────────────────────────────────
void audio_player_thread(const std::string& zmq_endpoint) {
    spdlog::info("[AUDIO Player] Subscribing to {}", zmq_endpoint);

    // ZMQ subscriber
    zmq::context_t ctx(1);
    zmq::socket_t sub(ctx, zmq::socket_type::sub);
    sub.set(zmq::sockopt::rcvtimeo, 500);
    sub.connect(zmq_endpoint);
    sub.set(zmq::sockopt::subscribe, "");

    // GStreamer playback pipeline: appsrc → audioconvert → autoaudiosink
    gst_init(nullptr, nullptr);

    GstElement* pipeline = gst_parse_launch(
        "appsrc name=src is-live=true format=time ! "
        "audioconvert ! audioresample ! "
        "queue ! "
        "autoaudiosink sync=false",
        nullptr
    );

    GstElement* appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "src");
    bool caps_set = false;

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    spdlog::info("[AUDIO Player] Pipeline started. Waiting for samples...");

    while (g_running) {
        zmq::message_t msg;
        auto res = sub.recv(msg, zmq::recv_flags::none);
        if (!res) continue;

        auto frame = oro::media::GetAudioFrame(msg.data());
        if (!frame || !frame->samples()) continue;

        uint32_t rate = frame->sample_rate();
        uint32_t channels = frame->channels();

        // Set caps on first frame
        if (!caps_set) {
            GstCaps* caps = gst_caps_new_simple("audio/x-raw",
                "format", G_TYPE_STRING, "S16LE",
                "rate", G_TYPE_INT, (int)rate,
                "channels", G_TYPE_INT, (int)channels,
                "layout", G_TYPE_STRING, "interleaved",
                nullptr
            );
            g_object_set(G_OBJECT(appsrc), "caps", caps, nullptr);
            gst_caps_unref(caps);
            caps_set = true;
            spdlog::info("[AUDIO Player] Caps set: {}Hz, {} ch, S16LE", rate, channels);
        }

        const uint8_t* data = frame->samples()->data();
        size_t size = frame->samples()->size();

        GstBuffer* buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
        gst_buffer_fill(buffer, 0, data, size);

        GstFlowReturn ret;
        g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
        gst_buffer_unref(buffer);
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(appsrc);
    gst_object_unref(pipeline);
    spdlog::info("[AUDIO Player] Stopped.");
}

// ─── Main ────────────────────────────────────────────────────────────────────
int main() {
    spdlog::info("=== Stream Player ===");
    spdlog::info("Validating full round-trip: HW → GStreamer → FlatBuffers → ZMQ → Display/Speaker");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::string video_endpoint = "ipc:///tmp/video.sock";
    std::string audio_endpoint = "ipc:///tmp/audio.sock";

    std::thread video_thread(video_player_thread, video_endpoint);
    std::thread audio_thread(audio_player_thread, audio_endpoint);

    video_thread.join();
    audio_thread.join();

    spdlog::info("Stream Player shut down cleanly.");
    return 0;
}
