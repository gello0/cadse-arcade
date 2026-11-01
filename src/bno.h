/* BNO085 9-axis IMU with on-chip sensor fusion, on the shared I2C bus
 *
 * Wiring (via the 2x20 connectors, e.g. on the Experiment PCB):
 *   VIN -> 3V3   (H1 pin 5 or 6)
 *   GND -> GND   (H1 pin 1 or 2)
 *   SDA -> GPIO8 (H2 pin 17)
 *   SCL -> GPIO9 (H2 pin 18)
 *   RST -> GPIO40 (H2 pin 30), optional: lets the firmware hard-reset the sensor
 * The core board already has 10k pull-ups on SDA/SCL. Address 0x4A or 0x4B
 * (depends on the breakout's ADR/DI pin), both are tried.
 */
#pragma once

#include <Arduino.h>

const int bnoResetPin = 40;

struct ImuSample {
  uint32_t ms;            // millis() of the latest rotation vector
  float q[4];             // quaternion w, x, y, z
  float ypr[3];           // yaw, pitch, roll in degrees
  float lin[3];           // linear acceleration (gravity removed), m/s^2
  float gyr[3];           // calibrated gyro, rad/s
  float mag[3];           // calibrated magnetic field, uT
  uint8_t accuracy;       // rotation vector accuracy 0 (unreliable) .. 3 (high)
};

bool bnoBegin();                 // safe to call again, retries if not found yet
bool bnoAvailable();
void bnoPoll();                  // call as often as possible
const ImuSample &bnoLatest();
float bnoRateHz();               // rotation vector reports per second
