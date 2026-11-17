// SPDX-License-Identifier: GPL-3.0-only
#include "common/qr.h"

#include <cstring>
#include <memory>
#include <vector>

extern "C" {
#include "quirc.h"
}

namespace mz {
namespace {

// The decoder keeps large work buffers, so one instance is reused.
struct quirc* q = nullptr;
int q_w = 0, q_h = 0;
// Pictures worked on along the way, kept so they are not allocated per frame.
std::vector<uint8_t> wide, rows, half, mask, part, sharp, big, bigger;

}  // namespace

void qr_release()
{
    if (q) quirc_destroy(q);
    q   = nullptr;
    q_w = q_h = 0;
    for (std::vector<uint8_t>* v : {&wide, &rows, &half, &mask, &part, &sharp, &big, &bigger}) std::vector<uint8_t>().swap(*v);
}

std::string qr_decode(const uint8_t* gray, int width, int height)
{
    if (!gray || width <= 0 || height <= 0) return {};

    if (!q) q = quirc_new();
    if (!q) return {};
    if (q_w != width || q_h != height) {
        if (quirc_resize(q, width, height) < 0) return {};
        q_w = width;
        q_h = height;
    }

    uint8_t* image = quirc_begin(q, nullptr, nullptr);
    memcpy(image, gray, static_cast<size_t>(width) * static_cast<size_t>(height));
    quirc_end(q);

    // These two are several kilobytes each: keep them off the stack.
    static const std::unique_ptr<quirc_code> code(new quirc_code);
    static const std::unique_ptr<quirc_data> data(new quirc_data);
    const int count = quirc_count(q);
    for (int i = 0; i < count; ++i) {
        quirc_extract(q, i, code.get());
        quirc_decode_error_t err = quirc_decode(code.get(), data.get());
        if (err == QUIRC_ERROR_DATA_ECC) {
            // A mirrored code (seen through glass, or a flipped sensor).
            quirc_flip(code.get());
            err = quirc_decode(code.get(), data.get());
        }
        if (err == QUIRC_SUCCESS && data->payload_len > 0) {
            return std::string(reinterpret_cast<const char*>(data->payload), static_cast<size_t>(data->payload_len));
        }
    }
    return {};
}

namespace {

// Unsharp mask: pushes each pixel away from the average of its 3x3
// surroundings, `strength` times over. Undoes some of a lens's blur.
void sharpen(const uint8_t* in, size_t w, size_t h, int strength, std::vector<uint8_t>& out)
{
    out.assign(in, in + w * h);
    for (size_t y = 1; y + 1 < h; ++y) {
        const uint8_t* a = in + (y - 1) * w;
        const uint8_t* b = in + y * w;
        const uint8_t* c = in + (y + 1) * w;
        uint8_t* o       = out.data() + y * w;
        for (size_t x = 1; x + 1 < w; ++x) {
            const int sum = a[x - 1] + a[x] + a[x + 1] + b[x - 1] + b[x] + b[x + 1] + c[x - 1] + c[x] + c[x + 1];
            const int v   = (9 * (1 + strength) * b[x] - strength * sum) / 9;
            o[x]          = static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }
}

// Doubles the size with smooth interpolation, across and then down.
void enlarge(const uint8_t* in, size_t w, size_t h, std::vector<uint8_t>& out)
{
    wide.resize(2 * w * h);
    for (size_t y = 0; y < h; ++y) {
        const uint8_t* row = in + y * w;
        uint8_t* o         = wide.data() + y * 2 * w;
        for (size_t x = 0; x < w; ++x) {
            const int left  = row[x > 0 ? x - 1 : 0];
            const int right = row[x + 1 < w ? x + 1 : w - 1];
            o[2 * x]        = static_cast<uint8_t>((3 * row[x] + left + 2) / 4);
            o[2 * x + 1]    = static_cast<uint8_t>((3 * row[x] + right + 2) / 4);
        }
    }
    out.resize(4 * w * h);
    for (size_t y = 0; y < h; ++y) {
        const uint8_t* row   = wide.data() + y * 2 * w;
        const uint8_t* above = wide.data() + (y > 0 ? y - 1 : 0) * 2 * w;
        const uint8_t* below = wide.data() + (y + 1 < h ? y + 1 : h - 1) * 2 * w;
        uint8_t* o0          = out.data() + (2 * y) * 2 * w;
        uint8_t* o1          = o0 + 2 * w;
        for (size_t x = 0; x < 2 * w; ++x) {
            o0[x] = static_cast<uint8_t>((3 * row[x] + above[x] + 2) / 4);
            o1[x] = static_cast<uint8_t>((3 * row[x] + below[x] + 2) / 4);
        }
    }
}

// Replaces each pixel with the darkest one within `reach` pixels of it.
void grow_dark(const uint8_t* in, size_t w, size_t h, size_t reach, std::vector<uint8_t>& out)
{
    rows.resize(w * h);
    for (size_t y = 0; y < h; ++y) {
        const uint8_t* row = in + y * w;
        uint8_t* o         = rows.data() + y * w;
        for (size_t x = 0; x < w; ++x) {
            const size_t from = x > reach ? x - reach : 0, to = x + reach < w ? x + reach : w - 1;
            uint8_t darkest   = row[from];
            for (size_t i = from + 1; i <= to; ++i) darkest = row[i] < darkest ? row[i] : darkest;
            o[x] = darkest;
        }
    }
    out.resize(w * h);
    for (size_t y = 0; y < h; ++y) {
        const size_t from = y > reach ? y - reach : 0, to = y + reach < h ? y + reach : h - 1;
        uint8_t* o        = out.data() + y * w;
        memcpy(o, rows.data() + from * w, w);
        for (size_t i = from + 1; i <= to; ++i) {
            const uint8_t* row = rows.data() + i * w;
            for (size_t x = 0; x < w; ++x) o[x] = row[x] < o[x] ? row[x] : o[x];
        }
    }
}

}  // namespace

std::string qr_decode_photo(const uint8_t* gray, int width, int height, unsigned attempt)
{
    if (!gray || width < 16 || height < 16) return {};
    const size_t w = static_cast<size_t>(width), h = static_cast<size_t>(height);

    // Half size, each pixel the average of four: quick, and it evens out
    // noise and slight blur. Reads a code that is large in the picture.
    const size_t hw = w / 2, hh = h / 2;
    half.resize(hw * hh);
    for (size_t y = 0; y < hh; ++y) {
        const uint8_t* a = gray + 2 * y * w;
        const uint8_t* b = a + w;
        uint8_t* o       = half.data() + y * hw;
        for (size_t x = 0; x < hw; ++x) o[x] = static_cast<uint8_t>((a[2 * x] + a[2 * x + 1] + b[2 * x] + b[2 * x + 1] + 2) / 4);
    }
    std::string text = qr_decode(half.data(), static_cast<int>(hw), static_cast<int>(hh));
    if (!text.empty()) return text;

    // One more way of looking at it per call, so the work per picture stays
    // bounded while a code held in view gets every treatment in turn.
    switch (attempt % 4) {
        case 0:
        case 2: {
            // A bright screen in a dark room is overexposed: its white bleeds
            // into the dark squares and thins them. Grow the dark back, then
            // count as dark whatever falls clearly short of white.
            size_t histogram[256] = {};
            for (const uint8_t v : half) ++histogram[v];
            size_t above = 0;
            int white    = 255;
            while (white > 0 && above + histogram[white] < half.size() / 200) above += histogram[white--];
            const bool wide = attempt % 4 == 0;
            const int cut   = white * (wide ? 75 : 84) / 100;
            grow_dark(gray, w, h, wide ? 2 : 1, mask);
            for (uint8_t& v : mask) v = v >= cut ? 255 : 0;
            return qr_decode(mask.data(), width, height);
        }
        case 1: {
            // The middle two thirds, sharpened hard and doubled: a smaller code.
            const size_t pw = w * 2 / 3 & ~size_t{1}, ph = h * 2 / 3 & ~size_t{1};
            part.resize(pw * ph);
            for (size_t y = 0; y < ph; ++y) memcpy(part.data() + y * pw, gray + (y + (h - ph) / 2) * w + (w - pw) / 2, pw);
            sharpen(part.data(), pw, ph, 4, sharp);
            enlarge(sharp.data(), pw, ph, big);
            return qr_decode(big.data(), static_cast<int>(pw * 2), static_cast<int>(ph * 2));
        }
        default: {
            // The middle third at four times the size: a code that is far away.
            const size_t pw = w / 3 & ~size_t{1}, ph = h / 3 & ~size_t{1};
            part.resize(pw * ph);
            for (size_t y = 0; y < ph; ++y) memcpy(part.data() + y * pw, gray + (y + (h - ph) / 2) * w + (w - pw) / 2, pw);
            sharpen(part.data(), pw, ph, 2, sharp);
            enlarge(sharp.data(), pw, ph, big);
            enlarge(big.data(), pw * 2, ph * 2, bigger);
            return qr_decode(bigger.data(), static_cast<int>(pw * 4), static_cast<int>(ph * 4));
        }
    }
}

}  // namespace mz
