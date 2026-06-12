#include "media/cam_splitter_node.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <cstring>
#include <cerrno>
#include <poll.h>
#include <spdlog/spdlog.h>

namespace oro {
namespace media {
namespace video {

CamSplitterNode::CamSplitterNode(const std::string& source_device,
                                 const std::string& cv_device,
                                 const std::string& videocall_device,
                                 const std::string& buffer_device,
                                 int width,
                                 int height,
                                 int framerate_num,
                                 const std::string& sink_pixelformat)
    : source_device_(source_device),
      cv_device_(cv_device),
      videocall_device_(videocall_device),
      buffer_device_(buffer_device),
      width_(width),
      height_(height),
      framerate_num_(framerate_num),
      sink_pixelformat_(sink_pixelformat) {
    memset(buffers_, 0, sizeof(buffers_));
}

CamSplitterNode::~CamSplitterNode() {
    stop();
}

int CamSplitterNode::get_fd() const {
    return pipe_fds_[0];
}

void CamSplitterNode::set_privacy_mode(bool enable) {
    if (enable) {
        if (!privacy_active_.exchange(true)) {
            spdlog::info("CamSplitterNode: Enabling privacy mode...");
            if (streaming_ && source_fd_ >= 0) {
                enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (xioctl(source_fd_, VIDIOC_STREAMOFF, &type) == -1) {
                    spdlog::error("VIDIOC_STREAMOFF failed in privacy mode: {}", strerror(errno));
                } else {
                    streaming_ = false;
                    spdlog::info("CamSplitterNode: Hardware streaming stopped.");
                }
            }
        }
    } else {
        if (privacy_active_.exchange(false)) {
            spdlog::info("CamSplitterNode: Disabling privacy mode...");
            if (!streaming_ && source_fd_ >= 0) {
                // Re-queue all buffers
                for (int i = 0; i < num_buffers_mapped_; ++i) {
                    struct v4l2_buffer buf{};
                    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                    buf.memory = V4L2_MEMORY_MMAP;
                    buf.index = i;
                    if (xioctl(source_fd_, VIDIOC_QBUF, &buf) == -1) {
                        spdlog::error("VIDIOC_QBUF failed during resume for buffer {}: {}", i, strerror(errno));
                    }
                }
                enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (xioctl(source_fd_, VIDIOC_STREAMON, &type) == -1) {
                    spdlog::error("VIDIOC_STREAMON failed in privacy mode: {}", strerror(errno));
                } else {
                    streaming_ = true;
                    spdlog::info("CamSplitterNode: Hardware streaming resumed.");
                }
            }
        }
    }
}

int CamSplitterNode::xioctl(int fd, unsigned long request, void *arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

bool CamSplitterNode::start() {
    if (running_.exchange(true)) {
        return true;
    }

    if (pipe(pipe_fds_) != 0) {
        spdlog::error("Failed to create pipe for CamSplitterNode: {}", strerror(errno));
        running_ = false;
        return false;
    }

#ifdef F_SETPIPE_SZ
    // Bump pipe size to 512 KB to handle MJPEG frames and prevent blocking
    if (fcntl(pipe_fds_[1], F_SETPIPE_SZ, 512 * 1024) < 0) {
        spdlog::warn("Failed to set pipe size: {}", strerror(errno));
    }
#endif

    if (!openSource() || !openSinks()) {
        cleanup();
        running_ = false;
        return false;
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(source_fd_, VIDIOC_STREAMON, &type) == -1) {
        if (errno == EPROTO || errno == EIO) {
            spdlog::warn("VIDIOC_STREAMON failed with {} — startup will not retry", strerror(errno));
        } else {
            spdlog::error("VIDIOC_STREAMON failed: {}", strerror(errno));
        }
        cleanup();
        running_ = false;
        return false;
    }
    streaming_ = true;

    // Set non-blocking mode after streaming starts for poll/DQBUF operations
    int flags = fcntl(source_fd_, F_GETFL, 0);
    if (flags != -1) {
        fcntl(source_fd_, F_SETFL, flags | O_NONBLOCK);
    }

    worker_ = std::thread([this]() { captureLoop(); });

    spdlog::info("CamSplitterNode started with native V4L2 capture.");
    return true;
}

void CamSplitterNode::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    // Signal the kernel to stop streaming BEFORE joining the worker.
    // This causes poll() / VIDIOC_DQBUF to return immediately so the
    // capture loop can observe running_==false without waiting the full
    // 1-second poll timeout.
    if (streaming_ && source_fd_ >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(source_fd_, VIDIOC_STREAMOFF, &type);
        streaming_ = false;
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    cleanup();
    spdlog::info("CamSplitterNode stopped.");
}

bool CamSplitterNode::openSource() {
    source_fd_ = open(source_device_.c_str(), O_RDWR, 0);
    if (source_fd_ < 0) {
        spdlog::error("Cannot open source device {}: {}", source_device_, strerror(errno));
        return false;
    }

    struct v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(source_fd_, VIDIOC_S_FMT, &fmt) == -1) {
        spdlog::error("Setting format on {} failed: {}", source_device_, strerror(errno));
        return false;
    }

    if (fmt.fmt.pix.width != static_cast<__u32>(width_) || fmt.fmt.pix.height != static_cast<__u32>(height_)) {
        spdlog::warn("Camera adjusted resolution from {}x{} to {}x{}", width_, height_, fmt.fmt.pix.width, fmt.fmt.pix.height);
        width_ = fmt.fmt.pix.width;
        height_ = fmt.fmt.pix.height;
    }

    // Set framerate
    struct v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(source_fd_, VIDIOC_G_PARM, &parm) != -1) {
        if (parm.parm.capture.capability & V4L2_CAP_TIMEPERFRAME) {
            parm.parm.capture.timeperframe.numerator = 1;
            parm.parm.capture.timeperframe.denominator = framerate_num_;
            if (xioctl(source_fd_, VIDIOC_S_PARM, &parm) == -1) {
                spdlog::warn("Setting framerate to {} on {} failed: {}", 
                             framerate_num_, source_device_, strerror(errno));
            } else {
                spdlog::info("Framerate set to {} fps", framerate_num_);
            }
        }
    }

    struct v4l2_requestbuffers req{};
    req.count = NUM_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(source_fd_, VIDIOC_REQBUFS, &req) == -1) {
        spdlog::error("Requesting buffers failed: {}", strerror(errno));
        return false;
    }

    if (req.count < 2) {
        spdlog::error("Insufficient buffer memory on {}", source_device_);
        return false;
    }

    num_buffers_mapped_ = req.count;

    for (int i = 0; i < num_buffers_mapped_; ++i) {
        struct v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(source_fd_, VIDIOC_QUERYBUF, &buf) == -1) {
            spdlog::error("Querying buffer {} failed: {}", i, strerror(errno));
            return false;
        }

        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(NULL, buf.length,
                                 PROT_READ | PROT_WRITE,
                                 MAP_SHARED,
                                 source_fd_, buf.m.offset);

        if (MAP_FAILED == buffers_[i].start) {
            spdlog::error("mmap failed: {}", strerror(errno));
            return false;
        }

        // Queue buffer
        if (xioctl(source_fd_, VIDIOC_QBUF, &buf) == -1) {
            spdlog::error("VIDIOC_QBUF failed: {}", strerror(errno));
            return false;
        }
    }

    return true;
}

bool CamSplitterNode::openSinks() {
    // Resolve the configured pixel-format string to a V4L2 fourcc constant.
    // This value comes from media_config.json:video.sink_pixelformat.
    __u32 fourcc = V4L2_PIX_FMT_MJPEG; // safe default
    if (sink_pixelformat_ == "YUYV") {
        fourcc = V4L2_PIX_FMT_YUYV;
    } else if (sink_pixelformat_ != "MJPG") {
        spdlog::warn("CamSplitterNode: unknown sink_pixelformat '{}', defaulting to MJPG",
                     sink_pixelformat_);
    }

    const std::string sink_paths[] = {cv_device_, videocall_device_, buffer_device_};
    const char*       sink_labels[] = {"CV(video11)", "VideoCall(video12)", "Buffer(video13)"};

    for (int i = 0; i < 3; ++i) {
        if (sink_paths[i].empty()) continue;

        int fd = open(sink_paths[i].c_str(), O_WRONLY | O_NONBLOCK, 0);
        if (fd < 0) {
            spdlog::error("Cannot open sink {} ({}): {}",
                          sink_labels[i], sink_paths[i], strerror(errno));
            return false;
        }

        struct v4l2_format fmt{};
        fmt.type                = V4L2_BUF_TYPE_VIDEO_OUTPUT;
        fmt.fmt.pix.width       = static_cast<__u32>(width_);
        fmt.fmt.pix.height      = static_cast<__u32>(height_);
        fmt.fmt.pix.pixelformat = fourcc;
        fmt.fmt.pix.field       = V4L2_FIELD_NONE;

        if (xioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
            spdlog::error("Setting format ({}) on sink {} ({}) failed: {}",
                          sink_pixelformat_, sink_labels[i], sink_paths[i], strerror(errno));
            close(fd);
            return false;
        }

        sink_fds_[i] = fd;
        spdlog::info("Sink {} ({}) opened — {}x{} {}",
                     sink_labels[i], sink_paths[i], width_, height_, sink_pixelformat_);
    }

    return true;
}

void CamSplitterNode::captureLoop() {
    while (running_) {
        if (privacy_active_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        struct pollfd fds;
        fds.fd = source_fd_;
        fds.events = POLLIN;
        
        int r = poll(&fds, 1, 1000); // 1s timeout
        if (r == -1) {
            if (errno != EINTR) {
                spdlog::error("Poll error in capture loop: {}", strerror(errno));
            }
            continue;
        }
        if (r == 0) {
            continue;
        }

        struct v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(source_fd_, VIDIOC_DQBUF, &buf) == -1) {
            if (errno == EAGAIN) continue;
            spdlog::error("VIDIOC_DQBUF failed: {}", strerror(errno));
            break;
        }

        uint8_t* frame = static_cast<uint8_t*>(buffers_[buf.index].start);
        size_t size = buf.bytesused;

        // Write to Virtual Loopback devices
        for (int i = 0; i < 3; ++i) {
            if (sink_fds_[i] >= 0) {
                // Non-blocking write. If buffer full, frame is dropped.
                if (::write(sink_fds_[i], frame, size) < 0 && errno != EAGAIN) {
                    spdlog::debug("Write to sink {} failed: {}", i, strerror(errno));
                }
            }
        }

        // Write to GStreamer pipeline pipe
        if (pipe_fds_[1] >= 0) {
            // Write to pipe blocking
            if (::write(pipe_fds_[1], frame, size) < 0) {
                spdlog::debug("Write to pipe failed: {}", strerror(errno));
            }
        }

        // Re-queue buffer
        if (xioctl(source_fd_, VIDIOC_QBUF, &buf) == -1) {
            spdlog::error("VIDIOC_QBUF failed after processing: {}", strerror(errno));
            break;
        }
    }
}

void CamSplitterNode::cleanup() {
    if (streaming_) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(source_fd_, VIDIOC_STREAMOFF, &type);
        streaming_ = false;
    }

    for (int i = 0; i < num_buffers_mapped_; ++i) {
        if (buffers_[i].start) {
            munmap(buffers_[i].start, buffers_[i].length);
            buffers_[i].start = nullptr;
        }
    }
    num_buffers_mapped_ = 0;

    if (source_fd_ >= 0) { close(source_fd_); source_fd_ = -1; }
    
    for (int i = 0; i < 3; ++i) {
        if (sink_fds_[i] >= 0) { close(sink_fds_[i]); sink_fds_[i] = -1; }
    }

    if (pipe_fds_[0] >= 0) { close(pipe_fds_[0]); pipe_fds_[0] = -1; }
    if (pipe_fds_[1] >= 0) { close(pipe_fds_[1]); pipe_fds_[1] = -1; }
}

} // namespace video
} // namespace media
} // namespace oro