/* Hardware access for the CADSE Core + Head PCB
 *
 * Pin numbers are taken from the cadse_core and cadse_head schematics.
 */
#pragma once

#include <Arduino.h>
#include <Adafruit_SSD1306.h>

// ----- I2C (OLED, port expander) -----
const int sdaPin = 8;
const int sclPin = 9;
const uint8_t oledAddress = 0x3C;
const uint8_t expanderAddress = 0x20;

// ----- IMU (LSM6DS3TR-C on SPI2) -----
const int imuCsPin = 10;
const int imuMosiPin = 11;
const int imuSckPin = 12;
const int imuMisoPin = 13;

// ----- touch pads -----
const int touchUpPin = 1;
const int touchLeftPin = 2;
const int touchXPin = 4;
const int touchDownPin = 5;
const int touchRightPin = 6;
const int touchThreshold = 60000;

// ----- bumper buttons (left pulls to 3V3, right pulls to GND) -----
const int bumperLeftPin = 16;
const int bumperRightPin = 15;

// ------ buzzer ------
const int channelBuzzer = 0;
const int buzzerPin = 14;

// ------ status led (active low, shared with boot button) ------
const int statusLedPin = 0;
const int channelStatusLed = 2;

// ------ battery and USB sense (voltage dividers into the ADC) ------
const int batterySensePin = 7;                  // V_BAT -> 470k / 150k
const float batteryDivider = (470.0f + 150.0f) / 150.0f;
const int usbSensePin = 3;                      // +5V -> 220k / 470k
const int usbPresentMv = 2000;                  // ~3.4 V at the pin with USB plugged in
const float batteryFullVolts = 4.15f;

// ------ micro SD (SD_MMC, 4 bit) ------
const int sdClkPin = 18;
const int sdCmdPin = 17;
const int sdD0Pin = 21;
const int sdD1Pin = 38;
const int sdD2Pin = 47;
const int sdD3Pin = 48;

// Tilt steering defaults: which accelerometer axis is left/right on screen and
// in which direction. The other axis is up/down. TILT SETUP in the menu
// overrides these and stores the result in flash (NVS).
#define TILT_X_AXIS 1   // 0 = x, 1 = y
#define TILT_X_SIGN -1  // 1 or -1
#define TILT_Y_SIGN 1   // 1 or -1

extern Adafruit_SSD1306 display;

struct Buttons {
  bool up, down, left, right, x;   // touch pads
  bool fire;                       // left bumper
  bool shield;                     // right bumper
};

void hwBegin();
bool imuAvailable();
float readTilt();                  // left/right tilt in m/s^2
void readTilt2(float &x, float &y); // left/right and up/down tilt in m/s^2
void readAccelRaw(float &a, float &b); // raw accelerometer x and y, for calibration
bool readImu(float accel[3], float gyro[3], float &tempC);  // m/s^2, rad/s, deg C
void setTiltConfig(int xAxis, int xSign, int ySign);  // applies and saves to flash
extern int tiltXAxis, tiltXSign, tiltYSign;
void readButtons(Buttons &held, Buttons &pressed);
void logTouchValues();             // raw touch readings to Serial, for tuning touchThreshold

void rgbLed(int led, bool on);     // led 1 or 3 on the head PCB
void statusLed(bool on);

// LED brightness, saved to flash. The RGB LEDs sit on the port expander and
// can only be on or off, so LOW lights one colour channel (0..3) of each.
enum LedMode { LEDS_OFF, LEDS_LOW, LEDS_HIGH, NUM_LED_MODES };
extern int ledMode, ledChannel;
void setLedConfig(int mode, int channel);

// Power: call updatePower() regularly, it samples about once per second
enum PowerState { ON_BATTERY, CHARGING, CHARGED };
void updatePower();
float batteryVolts();
int batteryPercent();              // 0..100, from a typical LiPo discharge curve
PowerState powerState();

// Volume 0 (off) .. numVolumeLevels - 1 (loudest), saved to flash
const int numVolumeLevels = 5;
int getVolume();
void setVolume(int level);

// Non-blocking sound effects: pairs of {frequency, duration_ms}, ended by {0, 0}
void playSfx(const uint16_t *notes);
void updateSfx();

// Live tone for the synth: freq 0 = silence, widthEighths 1..4 = pulse width
// from thin (12.5 %) to square (50 %). Stops any running sound effect.
void toneOut(uint32_t freq, int widthEighths = 4);
