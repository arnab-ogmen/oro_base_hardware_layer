#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <sys/types.h>

namespace oro {
namespace media {
namespace video {

class CamSplitterNode {
public:
    CamSplitterNode(const std::string& source_device,
                    const std::string& cv_device,
                    const std::string& videocall_device,
                    const std::string& buffer_device,
                    int width,
                    int height,
                    int framerate_num);
    ~CamSplitterNode();

    bool start();
    void stop();
    int get_fd() const;

private:
    void run();

    std::string source_device_;
    std::string cv_device_;
    std::string videocall_device_;
    std::string buffer_device_;
    int width_;
    int height_;
    int framerate_num_;
    int source_fd_;
    pid_t child_pid_;
    std::atomic<bool> running_;
    std::thread worker_;
};

} // namespace video
} // namespace media
} // namespace oro
