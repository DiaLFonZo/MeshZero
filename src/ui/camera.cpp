// SPDX-License-Identifier: GPL-3.0-only
#include "ui/camera.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace mz {
namespace {

constexpr size_t kPlaneBytes = static_cast<size_t>(kCamW) * kCamH;
constexpr size_t kFrameBytes = kPlaneBytes * 3 / 2;  // YUV 4:2:0

std::atomic<bool> g_available{false};

// MESHZERO_CAMERA_CMD replaces the capture program, for working without the
// hardware. It must write raw 1920x1080 YUV 4:2:0 frames to its output.
const char* override_command()
{
    const char* cmd = getenv("MESHZERO_CAMERA_CMD");
    return cmd && cmd[0] ? cmd : nullptr;
}

std::string capture_command()
{
    if (const char* cmd = override_command()) return cmd;
    // The sensor is mounted upside down in the case, hence the rotation. A code
    // is usually on a bright screen with dark surroundings: exposing for the
    // middle of the picture, and a little under, keeps it from washing out.
    return "exec rpicam-vid --nopreview --timeout 0 --codec yuv420 --width " + std::to_string(kCamW) + " --height " +
           std::to_string(kCamH) + " --framerate 8 --rotation 180 --metering spot --ev -1 --flush --output -";
}

void run_probe()
{
    FILE* p = popen("rpicam-hello --list-cameras 2>/dev/null", "r");
    if (!p) return;
    bool found = false;
    char line[256];
    while (fgets(line, sizeof(line), p)) {
        // A camera is listed as "0 : imx219 [3280x2464 ...".
        const char* s = line;
        while (*s == ' ') ++s;
        if (*s >= '0' && *s <= '9' && strstr(s, " : ")) found = true;
    }
    pclose(p);
    g_available = found;
}

}  // namespace

void Camera::probe()
{
    if (override_command()) {
        g_available = true;
        return;
    }
    std::thread(run_probe).detach();
}

bool Camera::available()
{
    return g_available;
}

Camera::~Camera()
{
    stop();
}

bool Camera::start()
{
    if (running()) return true;
    failed_ = false;
    fill_   = 0;
    buf_.assign(kFrameBytes, 0);
    frame_.assign(kPlaneBytes, 0);

    int fds[2];
    if (pipe(fds) != 0) return false;
    const std::string cmd = capture_command();
    const pid_t pid       = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        setpgid(0, 0);  // its own group, so everything it starts can be stopped together
        dup2(fds[1], STDOUT_FILENO);
        const int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            dup2(null_fd, STDERR_FILENO);
        }
        for (int fd = 3; fd < 1024; ++fd) close(fd);
        signal(SIGPIPE, SIG_DFL);
        execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    close(fds[1]);
    fd_  = fds[0];
    pid_ = pid;
    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL) | O_NONBLOCK);
#ifdef F_SETPIPE_SZ
    fcntl(fd_, F_SETPIPE_SZ, 1 << 20);
#endif
    return true;
}

void Camera::stop()
{
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    if (pid_ <= 0) return;
    kill(-pid_, SIGTERM);
    int status = 0;
    for (int i = 0; i < 50; ++i) {
        if (waitpid(pid_, &status, WNOHANG) != 0) {
            pid_ = -1;
            return;
        }
        usleep(10000);
    }
    kill(-pid_, SIGKILL);
    waitpid(pid_, &status, 0);
    pid_ = -1;
}

bool Camera::poll()
{
    if (fd_ < 0) return false;
    bool fresh = false;
    // Take everything that is waiting, so the frame shown is the latest one.
    for (int frames = 0; frames < 4;) {
        const ssize_t n = read(fd_, buf_.data() + fill_, kFrameBytes - fill_);
        if (n > 0) {
            fill_ += static_cast<size_t>(n);
            if (fill_ == kFrameBytes) {
                memcpy(frame_.data(), buf_.data(), kPlaneBytes);
                fill_ = 0;
                fresh = true;
                ++frames;
            }
        } else if (n == 0) {
            failed_ = true;
            stop();
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                failed_ = true;
                stop();
            }
            break;
        }
    }
    return fresh;
}

}  // namespace mz
