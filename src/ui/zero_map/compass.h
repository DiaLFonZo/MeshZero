// SPDX-License-Identifier: GPL-3.0-only
// The device's compass: its magnetometer and accelerometer, read through the
// kernel's sensor files, give the way the top of the screen points.
#pragma once

namespace mz::compass {

// Whether the device has the two sensors.
bool available();

// Readings are taken only between start and stop, on a thread of their own.
void start();
void stop();

// Degrees clockwise from magnetic north, smoothed. False while there is no
// reading yet, or the compass has not been calibrated.
bool heading(double& deg);

// A magnetometer also measures the device's own magnets. Calibrating finds
// that share: between begin and end the device is turned every way, and what
// each direction read at most and at least gives the middle to take off.
bool calibrated();
void calibrate_begin();
// How far along it is, 0 to 100. At 100 every direction has been covered.
int calibrate_progress();
// Keeps the result when it is complete; false, and the old one stays, when not.
bool calibrate_end();
void calibrate_cancel();

// Whether the radar turns with the device; saved for the next start.
bool follow();
void set_follow(bool on);

}  // namespace mz::compass
