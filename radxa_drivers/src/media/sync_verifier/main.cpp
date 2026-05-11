#include <iostream>
#include <iomanip>
#include <zmq.hpp>
#include <spdlog/spdlog.h>
#include "video_frame_generated.h"
#include "audio_frame_generated.h"

int main() {
    spdlog::info("Starting Synchronization Verifier...");

    zmq::context_t ctx(1);
    
    // Video Subscriber
    zmq::socket_t video_sub(ctx, zmq::socket_type::sub);
    video_sub.connect("ipc:///tmp/video.sock");
    video_sub.set(zmq::sockopt::subscribe, "");

    // Audio Subscriber
    zmq::socket_t audio_sub(ctx, zmq::socket_type::sub);
    audio_sub.connect("ipc:///tmp/audio.sock");
    audio_sub.set(zmq::sockopt::subscribe, "");

    zmq::pollitem_t items[] = {
        { video_sub, 0, ZMQ_POLLIN, 0 },
        { audio_sub, 0, ZMQ_POLLIN, 0 }
    };

    uint64_t last_video_ts = 0;
    uint64_t last_audio_ts = 0;

    while (true) {
        zmq::poll(items, 2, -1);

        if (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t msg;
            auto res = video_sub.recv(msg, zmq::recv_flags::none);
            if (res) {
                auto frame = oro::media::GetVideoFrame(msg.data());
                last_video_ts = frame->timestamp_ns();
                double delta_ms = (last_video_ts > last_audio_ts ? last_video_ts - last_audio_ts : last_audio_ts - last_video_ts) / 1000000.0;
                
                std::cout << "[VIDEO] Payload: " << frame->data()->size() << " bytes"
                          << " | TS: " << last_video_ts 
                          << " | ID: " << frame->frame_id() 
                          << " | Sync Delta: " << std::fixed << std::setprecision(2) << delta_ms << " ms\n";
            }
        }
        
        if (items[1].revents & ZMQ_POLLIN) {
            zmq::message_t msg;
            auto res = audio_sub.recv(msg, zmq::recv_flags::none);
            if (res) {
                auto frame = oro::media::GetAudioFrame(msg.data());
                last_audio_ts = frame->timestamp_ns();
                double delta_ms = (last_video_ts > last_audio_ts ? last_video_ts - last_audio_ts : last_audio_ts - last_video_ts) / 1000000.0;
                
                std::cout << "[AUDIO] Payload: " << frame->samples()->size() << " bytes"
                          << " | TS: " << last_audio_ts 
                          << " | Sync Delta: " << std::fixed << std::setprecision(2) << delta_ms << " ms\n";
            }
        }
    }

    return 0;
}
