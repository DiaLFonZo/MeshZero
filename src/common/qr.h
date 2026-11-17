// SPDX-License-Identifier: GPL-3.0-only
// Reading QR codes out of camera frames.
#pragma once

#include <cstdint>
#include <string>

namespace mz {

// Looks for a QR code in a greyscale image (one byte per pixel, row by row)
// and returns its content. Empty when no code could be read.
std::string qr_decode(const uint8_t* gray, int width, int height);

// The same for camera pictures, where a code may be small, slightly out of
// focus or overexposed. Each call tries the picture as it is and in one more
// way, chosen by `attempt`: pass a running count while a code is in view.
std::string qr_decode_photo(const uint8_t* gray, int width, int height, unsigned attempt);

// Gives back the memory the two functions above keep between calls, several
// megabytes after camera pictures. Call when done scanning.
void qr_release();

}  // namespace mz
