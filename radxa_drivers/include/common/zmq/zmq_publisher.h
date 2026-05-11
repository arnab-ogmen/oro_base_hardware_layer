#pragma once

#include <zmq.hpp>
#include <string>
#include <memory>
#include <stdexcept>

namespace oro {
namespace media {
namespace zmq_ipc {

class ZmqPublisher {
public:
    ZmqPublisher(const std::string& endpoint, int hwm = 3) 
        : ctx_(1), socket_(ctx_, zmq::socket_type::pub) {
        
        socket_.set(zmq::sockopt::sndhwm, hwm);
        socket_.set(zmq::sockopt::rcvhwm, hwm);
        socket_.bind(endpoint);
    }

    void publish(const void* data, size_t size) {
        zmq::message_t msg(data, size);
        auto res = socket_.send(msg, zmq::send_flags::dontwait);
        if (!res) {
            // Drop message gracefully or handle backpressure if needed.
            // In a low-latency PUB/SUB scenario, HWM drops old messages anyway.
        }
    }

private:
    zmq::context_t ctx_;
    zmq::socket_t socket_;
};

} // namespace zmq_ipc
} // namespace media
} // namespace oro
