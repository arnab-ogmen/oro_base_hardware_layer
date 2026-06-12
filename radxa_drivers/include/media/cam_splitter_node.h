#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <sys/types.h>
#include <vector>

namespace oro {
namespace media {
namespace video {

struct MmapBuffer {
    void *start;
    size_t length;
};

class CamSplitterNode {
public:
    CamSplitterNode(const std::string& source_device,
                    const std::string& cv_device,
                    const std::string& videocall_device,
                    const std::string& buffer_device,
                    int width,
                    int height,
                    int framerate_num,
                    const std::string& sink_pixelformat = "MJPG");
    ~CamSplitterNode();

    bool start();
    void stop();
    int get_fd() const;
    void set_privacy_mode(bool enable);

private:
    bool openSource();
    bool openSinks();
    void captureLoop();
    void cleanup();

    static int xioctl(int fd, unsigned long request, void *arg);

    std::string source_device_;
    std::string cv_device_;
    std::string videocall_device_;
    std::string buffer_device_;
    int width_;
    int height_;
    int framerate_num_;
    std::string sink_pixelformat_;  ///< "MJPG" or "YUYV", from media_config.json

    int source_fd_{-1};          // /dev/video0 capture fd
    int sink_fds_[3]{-1,-1,-1};  // video11, video12, video13 write fds
    int pipe_fds_[2]{-1,-1};     // pipe_fds_[1] = write, [0] = read (→ get_fd())

    static constexpr int NUM_BUFFERS = 4;
    MmapBuffer buffers_[NUM_BUFFERS]{};
    int num_buffers_mapped_{0};

    std::atomic<bool> running_{false};
    std::thread worker_;
    bool streaming_{false};
    std::atomic<bool> privacy_active_{false};
};

} // namespace video
} // namespace media
} // namespace oro