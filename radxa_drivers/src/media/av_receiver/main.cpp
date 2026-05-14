/**
 * @file main.cpp
 * @brief AV Receiver — Subscribes to video + audio ZMQ streams, displays
 *        via OpenCV with full overlay, plays audio via ALSA, and re-publishes
 *        a combined AVFrame on ipc:///tmp/av.sock.
 *
 * Data flow:
 *   video_ingestor (VideoFrame/ZMQ) ──┐
 *                                     ├──► av_receiver ──► cv::imshow()
 *   audio_ingestor (AudioFrame/ZMQ) ──┘         │              │
 *                                                ▼              ▼
 *                                         ALSA playback    ZMQ PUB av.sock
 */

#include "common/logging/logger.h"
#include "common/config/config_parser.h"
#include "common/zmq/zmq_publisher.h"
#include "video_frame_generated.h"
#include "audio_frame_generated.h"
#include "av_frame_generated.h"

#include <zmq.hpp>
#include <flatbuffers/flatbuffers.h>
#include <opencv2/opencv.hpp>
#include <spdlog/spdlog.h>
#include <alsa/asoundlib.h>

#include <atomic>
#include <csignal>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <chrono>
#include <string>
#include <vector>

// ─── Globals ─────────────────────────────────────────────────────────────────

static std::atomic<bool> g_running{true};

static void signal_handler(int signum) {
    spdlog::info("Interrupt signal ({}) received. Shutting down...", signum);
    g_running = false;
}

// ─── Audio Playback ──────────────────────────────────────────────────────────

struct AudioChunk {
    std::vector<uint8_t> data;
    uint64_t timestamp_ns;
};

class AlsaPlayer {
public:
    explicit AlsaPlayer(const std::string& device, int rate, int channels)
        : device_(device), rate_(rate), channels_(channels), pcm_(nullptr) {}

    ~AlsaPlayer() { close(); }

    bool open() {
        int err = snd_pcm_open(&pcm_, device_.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
        if (err < 0) {
            spdlog::error("[ALSA] Cannot open {}: {}", device_, snd_strerror(err));
            return false;
        }

        snd_pcm_hw_params_t* params;
        snd_pcm_hw_params_alloca(&params);
        snd_pcm_hw_params_any(pcm_, params);
        snd_pcm_hw_params_set_access(pcm_, params, SND_PCM_ACCESS_RW_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm_, params, SND_PCM_FORMAT_S16_LE);
        
        unsigned int actual_rate = static_cast<unsigned int>(rate_);
        snd_pcm_hw_params_set_rate_near(pcm_, params, &actual_rate, nullptr);
        snd_pcm_hw_params_set_channels(pcm_, params, static_cast<unsigned int>(channels_));

        // Buffer: 4096 frames, period: 1024 frames
        snd_pcm_uframes_t buffer_size = 4096;
        snd_pcm_uframes_t period_size = 1024;
        snd_pcm_hw_params_set_buffer_size_near(pcm_, params, &buffer_size);
        snd_pcm_hw_params_set_period_size_near(pcm_, params, &period_size, nullptr);

        err = snd_pcm_hw_params(pcm_, params);
        if (err < 0) {
            spdlog::error("[ALSA] Cannot set hw params: {}", snd_strerror(err));
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
            return false;
        }

        snd_pcm_prepare(pcm_);
        spdlog::info("[ALSA] Opened {} @ {}Hz, {} ch, S16LE", device_, actual_rate, channels_);
        return true;
    }

    void write(const uint8_t* data, size_t size) {
        if (!pcm_) return;
        snd_pcm_uframes_t frames = size / (2 * channels_); // S16LE = 2 bytes per sample per channel
        snd_pcm_sframes_t written = snd_pcm_writei(pcm_, data, frames);
        if (written < 0) {
            snd_pcm_recover(pcm_, static_cast<int>(written), 1);
        }
    }

    void close() {
        if (pcm_) {
            snd_pcm_drain(pcm_);
            snd_pcm_close(pcm_);
            pcm_ = nullptr;
        }
    }

private:
    std::string device_;
    int rate_;
    int channels_;
    snd_pcm_t* pcm_;
};

// ─── Audio Thread ────────────────────────────────────────────────────────────

static std::mutex g_audio_mutex;
static std::deque<AudioChunk> g_audio_queue;
static constexpr size_t MAX_AUDIO_QUEUE = 16;

static void audio_playback_thread(const std::string& device, int rate, int channels) {
    AlsaPlayer player(device, rate, channels);
    if (!player.open()) {
        spdlog::error("[AUDIO] Failed to open ALSA device, audio playback disabled.");
        return;
    }

    while (g_running) {
        AudioChunk chunk;
        {
            std::lock_guard<std::mutex> lock(g_audio_mutex);
            if (g_audio_queue.empty()) {
                // No data yet, sleep briefly
            } else {
                chunk = std::move(g_audio_queue.front());
                g_audio_queue.pop_front();
            }
        }

        if (!chunk.data.empty()) {
            player.write(chunk.data.data(), chunk.data.size());
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    player.close();
    spdlog::info("[AUDIO] Playback thread stopped.");
}

// ─── Overlay Drawing ─────────────────────────────────────────────────────────

static double compute_rms_db(const uint8_t* pcm_data, size_t size) {
    if (size < 2) return -96.0;

    const int16_t* samples = reinterpret_cast<const int16_t*>(pcm_data);
    size_t num_samples = size / 2;
    double sum_sq = 0.0;

    for (size_t i = 0; i < num_samples; ++i) {
        double s = static_cast<double>(samples[i]) / 32768.0;
        sum_sq += s * s;
    }

    double rms = std::sqrt(sum_sq / static_cast<double>(num_samples));
    if (rms < 1e-10) return -96.0;
    return 20.0 * std::log10(rms);
}

static void draw_overlay(cv::Mat& frame,
                          int width, int height,
                          double fps,
                          uint64_t frame_id,
                          uint64_t timestamp_ns,
                          double sync_delta_ms,
                          double audio_db,
                          const std::string& format) {
    const auto white = cv::Scalar(255, 255, 255);
    const auto green = cv::Scalar(0, 220, 0);
    const auto yellow = cv::Scalar(0, 220, 220);
    const auto red = cv::Scalar(0, 0, 220);
    const auto bg = cv::Scalar(0, 0, 0);
    const double font_scale = 0.5;
    const int thickness = 1;
    const int font = cv::FONT_HERSHEY_SIMPLEX;

    // Top-left: resolution | FPS | frame ID
    char top_line[128];
    std::snprintf(top_line, sizeof(top_line),
                  "%dx%d %s | %.1f FPS | ID: %lu",
                  width, height, format.c_str(), fps, frame_id);

    // Background rectangle for readability
    cv::rectangle(frame, cv::Point(0, 0), cv::Point(450, 22), bg, cv::FILLED);
    cv::putText(frame, top_line, cv::Point(5, 15), font, font_scale, green, thickness);

    int bottom_y = frame.rows;

    // VU meter bar (bottom)
    int vu_bar_y = bottom_y - 15;
    int vu_bar_h = 12;
    cv::rectangle(frame, cv::Point(0, vu_bar_y - 3), cv::Point(frame.cols, bottom_y), bg, cv::FILLED);

    // Clamp dB to [-60, 0]
    double clamped_db = std::max(-60.0, std::min(0.0, audio_db));
    double vu_ratio = (clamped_db + 60.0) / 60.0;
    int vu_width = static_cast<int>(vu_ratio * (frame.cols - 150));

    cv::Scalar vu_color = green;
    if (clamped_db > -6.0) vu_color = red;
    else if (clamped_db > -20.0) vu_color = yellow;

    cv::rectangle(frame, cv::Point(5, vu_bar_y), cv::Point(5 + vu_width, vu_bar_y + vu_bar_h), vu_color, cv::FILLED);
    // VU dB text
    char vu_text[32];
    std::snprintf(vu_text, sizeof(vu_text), "%.1f dB", audio_db);
    cv::putText(frame, vu_text, cv::Point(frame.cols - 140, vu_bar_y + 10), font, font_scale, white, thickness);

    // Bottom-left: timestamp + sync delta
    char bottom_line[128];
    std::snprintf(bottom_line, sizeof(bottom_line),
                  "TS: %lu ns | Sync: %+.1f ms",
                  timestamp_ns, sync_delta_ms);

    cv::rectangle(frame, cv::Point(0, vu_bar_y - 25), cv::Point(450, vu_bar_y - 3), bg, cv::FILLED);
    cv::putText(frame, bottom_line, cv::Point(5, vu_bar_y - 8), font, font_scale, white, thickness);
}

// ─── Main ────────────────────────────────────────────────────────────────────

int main() {
    oro::media::logging::init_logger("av_receiver");
    spdlog::info("=== AV Receiver ===");

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── Load config ──────────────────────────────────────────────────────────
    oro::media::config::MediaConfig config;
    try {
        config = oro::media::config::MediaConfig::load("../configs/media_config.json");
        spdlog::info("Config loaded.");
    } catch (const std::exception& e) {
        spdlog::warn("Config load failed, using defaults: {}", e.what());
    }

    const std::string video_endpoint = config.video.zmq_endpoint;
    const std::string audio_endpoint = config.audio.zmq_endpoint;
    const std::string av_endpoint = config.av.zmq_endpoint;
    const std::string alsa_device = config.av.audio_playback_device;

    spdlog::info("Video SUB: {}", video_endpoint);
    spdlog::info("Audio SUB: {}", audio_endpoint);
    spdlog::info("AV PUB:    {}", av_endpoint);
    spdlog::info("ALSA out:  {}", alsa_device);

    // ── ZMQ Subscribers ──────────────────────────────────────────────────────
    zmq::context_t ctx(1);

    zmq::socket_t video_sub(ctx, zmq::socket_type::sub);
    video_sub.set(zmq::sockopt::rcvtimeo, 500);
    video_sub.connect(video_endpoint);
    video_sub.set(zmq::sockopt::subscribe, "");

    zmq::socket_t audio_sub(ctx, zmq::socket_type::sub);
    audio_sub.set(zmq::sockopt::rcvtimeo, 500);
    audio_sub.connect(audio_endpoint);
    audio_sub.set(zmq::sockopt::subscribe, "");

    // ── ZMQ Publisher for combined AV ────────────────────────────────────────
    auto av_publisher = std::make_shared<oro::media::zmq_ipc::ZmqPublisher>(av_endpoint, config.av.hwm);

    // ── Start audio playback thread ──────────────────────────────────────────
    std::thread audio_thread(audio_playback_thread,
                             alsa_device,
                             config.audio.rate,
                             config.audio.channels);

    // ── State for correlation & overlay ──────────────────────────────────────
    uint64_t last_audio_ts = 0;
    double last_audio_db = -96.0;
    std::vector<uint8_t> last_audio_data;
    uint32_t last_audio_rate = static_cast<uint32_t>(config.audio.rate);
    uint32_t last_audio_channels = static_cast<uint32_t>(config.audio.channels);

    // FPS calculation
    uint64_t frame_count = 0;
    auto fps_start = std::chrono::steady_clock::now();
    double current_fps = 0.0;

    spdlog::info("Entering main loop. Waiting for frames...");

    // ── Main poll loop ───────────────────────────────────────────────────────
    zmq::pollitem_t items[] = {
        { static_cast<void*>(video_sub), 0, ZMQ_POLLIN, 0 },
        { static_cast<void*>(audio_sub), 0, ZMQ_POLLIN, 0 }
    };

    while (g_running) {
        zmq::poll(items, 2, 50);  // 50ms timeout to keep cv::waitKey responsive

        // ── Audio ────────────────────────────────────────────────────────────
        if (items[1].revents & ZMQ_POLLIN) {
            zmq::message_t msg;
            auto res = audio_sub.recv(msg, zmq::recv_flags::none);
            if (res) {
                auto aframe = oro::media::GetAudioFrame(msg.data());
                if (aframe && aframe->samples()) {
                    last_audio_ts = aframe->timestamp_ns();
                    last_audio_rate = aframe->sample_rate();
                    last_audio_channels = aframe->channels();

                    const uint8_t* pcm = aframe->samples()->data();
                    size_t pcm_size = aframe->samples()->size();

                    // Compute RMS for VU meter
                    last_audio_db = compute_rms_db(pcm, pcm_size);

                    // Latch latest audio data for AVFrame
                    last_audio_data.assign(pcm, pcm + pcm_size);

                    // Enqueue for ALSA playback
                    {
                        std::lock_guard<std::mutex> lock(g_audio_mutex);
                        if (g_audio_queue.size() < MAX_AUDIO_QUEUE) {
                            g_audio_queue.push_back({last_audio_data, last_audio_ts});
                        }
                        // else: drop — playback can't keep up
                    }
                }
            }
        }

        // ── Video ────────────────────────────────────────────────────────────
        if (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t msg;
            auto res = video_sub.recv(msg, zmq::recv_flags::none);
            if (res) {
                auto vframe = oro::media::GetVideoFrame(msg.data());
                if (!vframe || !vframe->data()) continue;

                uint32_t w = vframe->width();
                uint32_t h = vframe->height();
                uint64_t vid_ts = vframe->timestamp_ns();
                uint64_t fid = vframe->frame_id();
                const uint8_t* nv12_data = vframe->data()->data();
                size_t nv12_size = vframe->data()->size();
                std::string format_str = vframe->format() ? vframe->format()->str() : "NV12";

                // NV12 frame: height * 1.5 rows of width bytes
                cv::Mat nv12(static_cast<int>(h * 3 / 2), static_cast<int>(w), CV_8UC1,
                             const_cast<uint8_t*>(nv12_data));
                cv::Mat bgr;
                cv::cvtColor(nv12, bgr, cv::COLOR_YUV2BGR_NV12);

                // FPS calculation
                ++frame_count;
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - fps_start).count();
                if (elapsed >= 1.0) {
                    current_fps = static_cast<double>(frame_count) / elapsed;
                    frame_count = 0;
                    fps_start = now;
                }

                // Sync delta
                double sync_delta_ms = 0.0;
                if (last_audio_ts > 0) {
                    int64_t delta_ns = static_cast<int64_t>(vid_ts) - static_cast<int64_t>(last_audio_ts);
                    sync_delta_ms = static_cast<double>(delta_ns) / 1e6;
                }

                // Draw overlay
                draw_overlay(bgr,
                             static_cast<int>(w), static_cast<int>(h),
                             current_fps, fid, vid_ts,
                             sync_delta_ms, last_audio_db,
                             format_str);

                // Display
                cv::imshow("AV Receiver", bgr);

                // Publish combined AVFrame
                {
                    flatbuffers::FlatBufferBuilder builder(nv12_size + last_audio_data.size() + 1024);

                    auto fb_format = builder.CreateString(format_str);
                    auto fb_video = builder.CreateVector(nv12_data, nv12_size);
                    auto fb_audio = builder.CreateVector(last_audio_data.data(), last_audio_data.size());

                    int64_t sync_delta_us = 0;
                    if (last_audio_ts > 0) {
                        sync_delta_us = (static_cast<int64_t>(vid_ts) - static_cast<int64_t>(last_audio_ts)) / 1000;
                    }

                    oro::media::AVFrameBuilder av_builder(builder);
                    av_builder.add_timestamp_ns(vid_ts);
                    av_builder.add_width(w);
                    av_builder.add_height(h);
                    av_builder.add_format(fb_format);
                    av_builder.add_frame_id(fid);
                    av_builder.add_video_data(fb_video);
                    av_builder.add_sample_rate(last_audio_rate);
                    av_builder.add_channels(last_audio_channels);
                    av_builder.add_audio_data(fb_audio);
                    av_builder.add_av_sync_delta_us(sync_delta_us);

                    auto av_frame = av_builder.Finish();
                    builder.Finish(av_frame);

                    av_publisher->publish(builder.GetBufferPointer(), builder.GetSize());
                }
            }
        }

        // OpenCV event pump — also provides ~30fps cadence check
        int key = cv::waitKey(1);
        if (key == 27 || key == 'q') {  // ESC or q to quit
            spdlog::info("User pressed quit key.");
            g_running = false;
        }
    }

    cv::destroyAllWindows();

    // Wait for audio thread
    if (audio_thread.joinable()) {
        audio_thread.join();
    }

    spdlog::info("AV Receiver shut down cleanly.");
    return 0;
}
