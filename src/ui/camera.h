// SPDX-License-Identifier: GPL-3.0-only
// The built-in camera, read as a stream of greyscale frames. Capture is left
// to the system's rpicam-vid; this only starts it and collects its output.
#pragma once

#include <cstdint>
#include <vector>

#include <sys/types.h>

namespace mz {

// The sensor's unbinned 1080p mode: the lens is fixed-focus and only sharp from
// arm's length, so a code has to be readable while it is small in the picture.
constexpr int kCamW = 1920;
constexpr int kCamH = 1080;

class Camera {
public:
    Camera() = default;
    ~Camera();
    Camera(const Camera&)            = delete;
    Camera& operator=(const Camera&) = delete;

    // Starts looking for a camera in the background. Call once at startup.
    static void probe();
    // Whether this device has a camera. False until the check has finished,
    // and on models that come without one.
    static bool available();

    bool start();
    void stop();
    bool running() const { return pid_ > 0; }
    // True once the capture program has ended by itself.
    bool failed() const { return failed_; }
    // Collects what the camera has produced. True when a new frame is ready.
    bool poll();
    // The most recent frame: kCamW x kCamH bytes, one brightness value each.
    const uint8_t* frame() const { return frame_.data(); }

private:
    pid_t pid_ = -1;
    int fd_    = -1;
    bool failed_ = false;
    std::vector<uint8_t> buf_;    // frame being received (brightness + colour planes)
    size_t fill_ = 0;
    std::vector<uint8_t> frame_;  // last complete brightness plane
};

}  // namespace mz
