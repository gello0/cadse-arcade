/* Orientation from the onboard LSM6DS3 (accelerometer + gyro, no magnetometer)
 *
 * A complementary filter blends the gyro (smooth but drifting) with the
 * accelerometer's gravity direction (noisy but drift-free). Pitch and roll
 * stay accurate; yaw is gyro-only and drifts slowly, there is no compass.
 */
#pragma once

#include <Arduino.h>

struct OnboardSample {
  uint32_t ms;
  float acc[3];           // m/s^2, gravity included
  float gyr[3];           // rad/s
  float ypr[3];           // yaw, pitch, roll in degrees
  float q[4];             // same orientation as quaternion w, x, y, z
  float tempC;
};

void onboardReset();               // zero yaw and restart the filter
void onboardPoll();                // call often, runs the filter at up to 200 Hz
const OnboardSample &onboardLatest();
