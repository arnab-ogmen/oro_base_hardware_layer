#include "media/cam_splitter_node.h"
#include <chrono>
#include <cstring>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
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
                                 int framerate_num)
    : source_device_(source_device),
      cv_device_(cv_device),
      videocall_device_(videocall_device),
      buffer_device_(buffer_device),
      width_(width),
      height_(height),
      framerate_num_(framerate_num),
      source_fd_(-1),
      child_pid_(-1),
      running_(false) {
}

CamSplitterNode::~CamSplitterNode() {
    stop();
}

int CamSplitterNode::get_fd() const {
    return source_fd_;
}

bool CamSplitterNode::start() {
    if (running_.exchange(true)) {
        return true; // already running
    }

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        spdlog::error("Failed to create pipe for CamSplitterNode: {}", std::strerror(errno));
        running_ = false;
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        spdlog::error("Failed to fork v4l2-ctl process: {}", std::strerror(errno));
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        running_ = false;
        return false;
    }

    if (pid == 0) {
        close(pipe_fds[0]);
        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
            _exit(127);
        }
        close(pipe_fds[1]);

        std::string fmt_arg = "--set-fmt-video=width=" + std::to_string(width_) + ",height=" + std::to_string(height_) + ",pixelformat=MJPG";
        char* args[] = {
            (char*)"v4l2-ctl",
            (char*)"--device",
            const_cast<char*>(source_device_.c_str()),
            (char*)"--stream-mmap",
            (char*)"3",
            (char*)"--stream-to",
            (char*)"-",
            const_cast<char*>(fmt_arg.c_str()),
            nullptr
        };
        execvp("v4l2-ctl", args);

        _exit(127);
    }

    close(pipe_fds[1]);
    source_fd_ = pipe_fds[0];
    child_pid_ = pid;

    worker_ = std::thread([this] { run(); });
    spdlog::info("CamSplitterNode started for source device '{}' on fd {}", source_device_, source_fd_);
    return true;
}

void CamSplitterNode::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);
        int status = 0;
        waitpid(child_pid_, &status, 0);
        child_pid_ = -1;
    }

    if (source_fd_ >= 0) {
        close(source_fd_);
        source_fd_ = -1;
    }

    if (worker_.joinable()) {
        worker_.join();
    }
    spdlog::info("CamSplitterNode stopped");
}

void CamSplitterNode::run() {
    if (child_pid_ <= 0) {
        return;
    }

    int status = 0;
    waitpid(child_pid_, &status, 0);
    if (running_) {
        if (WIFEXITED(status)) {
            spdlog::warn("CamSplitterNode process exited unexpectedly with code {}", WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            spdlog::warn("CamSplitterNode process was terminated by signal {}", WTERMSIG(status));
        } else {
            spdlog::warn("CamSplitterNode process exited unexpectedly with status {}", status);
        }
    }
    running_ = false;
}

} // namespace video
} // namespace media
} // namespace oro
