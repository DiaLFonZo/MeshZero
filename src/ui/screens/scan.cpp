// SPDX-License-Identifier: GPL-3.0-only
// Camera view that looks for a QR code.
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "common/qr.h"
#include "common/util.h"
#include "ui/app.h"
#include "ui/camera.h"

namespace mz {
namespace {

constexpr int kPreviewW = 184;
constexpr int kPreviewH = 138;
// The middle of the camera picture is searched and shown, at full detail.
constexpr int kScanW = 1440;
constexpr int kScanH = 1080;

class ScanScreen : public Screen {
public:
    ScanScreen(App& a, std::string title, std::function<bool(const std::string&)> on_code)
        : Screen(a), title_(std::move(title)), on_code_(std::move(on_code))
    {
        pixels_.assign(static_cast<size_t>(kPreviewW) * kPreviewH, 0);
        crop_.assign(static_cast<size_t>(kScanW) * kScanH, 0);
        job_.assign(crop_.size(), 0);
        dsc_.header.magic  = LV_IMAGE_HEADER_MAGIC;
        dsc_.header.cf     = LV_COLOR_FORMAT_RGB565;
        dsc_.header.w      = kPreviewW;
        dsc_.header.h      = kPreviewH;
        dsc_.header.stride = kPreviewW * 2;
        dsc_.data          = reinterpret_cast<const uint8_t*>(pixels_.data());
        dsc_.data_size     = static_cast<uint32_t>(pixels_.size() * 2);

        image_ = lv_image_create(root);
        lv_image_set_src(image_, &dsc_);
        lv_obj_set_pos(image_, 2, 0);

        status_ = ui::label(root, "", ui::c_dim());
        lv_obj_set_pos(status_, kPreviewW + 8, 3);
        lv_obj_set_width(status_, kContentW - kPreviewW - 12);
        lv_label_set_long_mode(status_, LV_LABEL_LONG_WRAP);

        timer_ = lv_timer_create([](lv_timer_t* t) { static_cast<ScanScreen*>(lv_timer_get_user_data(t))->step(); },
                                 80, this);
        lv_timer_pause(timer_);
    }
    ~ScanScreen() override
    {
        stop_reader();
        qr_release();
        if (timer_) lv_timer_delete(timer_);
    }

    std::string title() const override { return title_; }
    std::string hint() const override { return "Esc back"; }

    void on_show() override
    {
        done_      = false;
        got_frame_ = false;
        if (camera_.start()) {
            ui::set_text(status_, "Starting the camera...");
            start_reader();
            lv_timer_resume(timer_);
        } else {
            ui::set_text(status_, "The camera could not be started");
        }
    }
    void on_hide() override
    {
        lv_timer_pause(timer_);
        camera_.stop();
        stop_reader();
        qr_release();
    }

private:
    // Searching a picture for a code takes long enough to make the preview
    // stutter, so it runs beside the interface, always on the newest picture.
    void start_reader()
    {
        if (reader_.joinable()) return;
        quit_ = has_job_ = busy_ = false;
        found_.clear();
        reader_ = std::thread([this]() {
            std::vector<uint8_t> picture;
            unsigned attempt = 0;
            std::unique_lock<std::mutex> lock(mutex_);
            while (true) {
                wake_.wait(lock, [this]() { return quit_ || has_job_; });
                if (quit_) return;
                picture.swap(job_);
                has_job_ = false;
                busy_    = true;
                lock.unlock();
                std::string text = qr_decode_photo(picture.data(), kScanW, kScanH, attempt++);
                lock.lock();
                job_.swap(picture);
                busy_ = false;
                if (!text.empty()) found_ = std::move(text);
            }
        });
    }
    void stop_reader()
    {
        if (!reader_.joinable()) return;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
        }
        wake_.notify_one();
        reader_.join();
    }

    void step()
    {
        if (done_) return;
        const bool fresh = camera_.poll();
        if (camera_.failed()) {
            ui::set_text(status_, "The camera stopped");
            lv_timer_pause(timer_);
            return;
        }
        if (fresh) {
            if (!got_frame_) {
                got_frame_ = true;
                ui::set_text(status_, "Hold the code in the middle, about two hand spans away");
            }
            const uint8_t* full =
                camera_.frame() + static_cast<size_t>((kCamH - kScanH) / 2) * kCamW + (kCamW - kScanW) / 2;
            for (int y = 0; y < kScanH; ++y) {
                memcpy(crop_.data() + static_cast<size_t>(y) * kScanW, full + static_cast<size_t>(y) * kCamW, kScanW);
            }
            draw(crop_.data());
        }

        std::string text;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            text.swap(found_);
            if (fresh && !has_job_ && !busy_) {
                memcpy(job_.data(), crop_.data(), crop_.size());
                has_job_ = true;
                wake_.notify_one();
            }
        }
        if (text.empty()) return;
        // The same unusable code stays in view for a while: report it once.
        const uint64_t now = now_ms();
        if (text == rejected_ && now - rejected_ms_ < 4000) return;
        if (on_code_(text)) {
            done_ = true;
            lv_timer_pause(timer_);
        } else {
            rejected_    = text;
            rejected_ms_ = now;
        }
    }

    void draw(const uint8_t* gray)
    {
        for (int y = 0; y < kPreviewH; ++y) {
            const uint8_t* row = gray + static_cast<size_t>(y * kScanH / kPreviewH) * kScanW;
            uint16_t* out      = pixels_.data() + static_cast<size_t>(y) * kPreviewW;
            for (int x = 0; x < kPreviewW; ++x) {
                const uint8_t g = row[x * kScanW / kPreviewW];
                out[x]          = static_cast<uint16_t>(((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3));
            }
        }
        lv_obj_invalidate(image_);
    }

    std::string title_;
    std::function<bool(const std::string&)> on_code_;
    Camera camera_;
    std::vector<uint16_t> pixels_;
    std::vector<uint8_t> crop_;
    std::thread reader_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<uint8_t> job_;  // picture handed to the reader
    bool has_job_ = false;
    bool busy_    = false;
    bool quit_    = false;
    std::string found_;
    lv_image_dsc_t dsc_{};
    lv_obj_t* image_   = nullptr;
    lv_obj_t* status_  = nullptr;
    lv_timer_t* timer_ = nullptr;
    bool done_         = false;
    bool got_frame_    = false;
    std::string rejected_;
    uint64_t rejected_ms_ = 0;
};

}  // namespace

std::unique_ptr<Screen> make_scanner(App& app, const std::string& title,
                                     std::function<bool(const std::string&)> on_code)
{
    return std::make_unique<ScanScreen>(app, title, std::move(on_code));
}

}  // namespace mz
