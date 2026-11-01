#include "hw.h"

#include <Wire.h>
#include <Adafruit_LSM6DS3TRC.h>
#include <Adafruit_MCP23X17.h>
#include <Preferences.h>

Adafruit_SSD1306 display(128, 64, &Wire, -1);

int tiltXAxis = TILT_X_AXIS;
int tiltXSign = TILT_X_SIGN;
int tiltYSign = TILT_Y_SIGN;

int ledMode = LEDS_HIGH;
int ledChannel = 0;
const uint32_t statusLowDuty = 20;   // of 255

static void loadTiltConfig();
static void writeStatus();

static Adafruit_LSM6DS3TRC imu;
static Adafruit_MCP23X17 expander;
static bool imuOk = false;
static bool expanderOk = false;

// RGB LEDs are common anode on the expander: LED1 = EPIO7..10, LED3 = EPIO12..15
static const uint8_t led1Pins[] = {7, 8, 9, 10};
static const uint8_t led3Pins[] = {12, 13, 14, 15};

// Volume is the PWM duty cycle of the tone: 50 % (512 of 1023) is loudest.
// Steps grow roughly exponentially because loudness isn't linear in duty.
static const uint16_t volumeDuty[numVolumeLevels] = {0, 8, 40, 160, 512};
static int volume = numVolumeLevels - 1;

// arduino-esp32 3.x addresses LEDC by pin, 2.x by channel. ledcWriteTone
// leaves the timer at 10 bit resolution, so the duty is written afterwards.
static void buzzerTone(uint32_t freq){
  uint32_t duty = (freq == 0) ? 0 : volumeDuty[volume];
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(buzzerPin, freq);
  if( freq ) ledcWrite(buzzerPin, duty);
#else
  ledcWriteTone(channelBuzzer, freq);
  if( freq ) ledcWrite(channelBuzzer, duty);
#endif
}

int getVolume(){
  return volume;
}

void setVolume(int level){
  volume = constrain(level, 0, numVolumeLevels - 1);
  Preferences prefs;
  prefs.begin("cadse", false);
  prefs.putChar("volume", volume);
  prefs.end();
}

void hwBegin(){
  Wire.begin(sdaPin, sclPin);
  if( !display.begin(SSD1306_SWITCHCAPVCC, oledAddress) ){
    Serial.println("OLED not found!");
  }
  display.clearDisplay();
  display.display();

  loadTiltConfig();
  imuOk = imu.begin_SPI(imuCsPin, imuSckPin, imuMisoPin, imuMosiPin);
  if( imuOk ){
    imu.setAccelRange(LSM6DS_ACCEL_RANGE_2_G);
    imu.setAccelDataRate(LSM6DS_RATE_104_HZ);
  } else {
    Serial.println("IMU not found, using touch steering only");
  }

  expanderOk = expander.begin_I2C(expanderAddress);
  if( expanderOk ){
    for( uint8_t p : led1Pins ){ expander.pinMode(p, OUTPUT); expander.digitalWrite(p, HIGH); }
    for( uint8_t p : led3Pins ){ expander.pinMode(p, OUTPUT); expander.digitalWrite(p, HIGH); }
  }

  pinMode(bumperLeftPin, INPUT_PULLDOWN);
  pinMode(bumperRightPin, INPUT_PULLUP);

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttachChannel(buzzerPin, 2000, 12, channelBuzzer);
  ledcAttachChannel(statusLedPin, 5000, 8, channelStatusLed);
#else
  ledcSetup(channelBuzzer, 2000, 12);
  ledcAttachPin(buzzerPin, channelBuzzer);
  ledcSetup(channelStatusLed, 5000, 8);
  ledcAttachPin(statusLedPin, channelStatusLed);
#endif
  writeStatus();
  buzzerTone(0);

  analogReadResolution(12);
  analogSetPinAttenuation(batterySensePin, ADC_11db);
  analogSetPinAttenuation(usbSensePin, ADC_11db);
  updatePower();
}

bool imuAvailable(){
  return imuOk;
}

bool readImu(float accel[3], float gyro[3], float &tempC){
  if( !imuOk ){
    return false;
  }
  sensors_event_t a, g, t;
  imu.getEvent(&a, &g, &t);
  accel[0] = a.acceleration.x;
  accel[1] = a.acceleration.y;
  accel[2] = a.acceleration.z;
  gyro[0] = g.gyro.x;
  gyro[1] = g.gyro.y;
  gyro[2] = g.gyro.z;
  tempC = t.temperature;
  return true;
}

void readAccelRaw(float &a, float &b){
  if( !imuOk ){
    a = b = 0;
    return;
  }
  sensors_event_t accel, gyro, temp;
  imu.getEvent(&accel, &gyro, &temp);
  a = accel.acceleration.x;
  b = accel.acceleration.y;
}

void readTilt2(float &x, float &y){
  float a, b;
  readAccelRaw(a, b);
  x = tiltXSign * ((tiltXAxis == 0) ? a : b);
  y = tiltYSign * ((tiltXAxis == 0) ? b : a);
}

void setTiltConfig(int xAxis, int xSign, int ySign){
  tiltXAxis = xAxis;
  tiltXSign = xSign;
  tiltYSign = ySign;
  Preferences prefs;
  prefs.begin("cadse", false);
  prefs.putChar("tiltAxis", xAxis);
  prefs.putChar("tiltXSign", xSign);
  prefs.putChar("tiltYSign", ySign);
  prefs.end();
}

static void loadTiltConfig(){
  Preferences prefs;
  prefs.begin("cadse", true);
  tiltXAxis = prefs.getChar("tiltAxis", TILT_X_AXIS);
  tiltXSign = prefs.getChar("tiltXSign", TILT_X_SIGN);
  tiltYSign = prefs.getChar("tiltYSign", TILT_Y_SIGN);
  volume = constrain((int)prefs.getChar("volume", numVolumeLevels - 1), 0, numVolumeLevels - 1);
  ledMode = constrain((int)prefs.getChar("ledMode", LEDS_HIGH), 0, NUM_LED_MODES - 1);
  ledChannel = constrain((int)prefs.getChar("ledChannel", 0), 0, 3);
  prefs.end();
}

float readTilt(){
  float x, y;
  readTilt2(x, y);
  return x;
}

static bool touched(int pin){
  return touchRead(pin) > touchThreshold;
}

void logTouchValues(){
  Serial.printf("touch raw (threshold %d): up %lu  left %lu  x %lu  down %lu  right %lu\n", touchThreshold,
                (unsigned long)touchRead(touchUpPin), (unsigned long)touchRead(touchLeftPin),
                (unsigned long)touchRead(touchXPin), (unsigned long)touchRead(touchDownPin),
                (unsigned long)touchRead(touchRightPin));
  Serial.printf("bumpers: left %s  right %s\n",
                digitalRead(bumperLeftPin) == HIGH ? "pressed" : "-",
                digitalRead(bumperRightPin) == LOW ? "pressed" : "-");
}

void readButtons(Buttons &held, Buttons &pressed){
  static Buttons last = {};
  static bool primed = false;
  Buttons now;
  now.up = touched(touchUpPin);
  now.down = touched(touchDownPin);
  now.left = touched(touchLeftPin);
  now.right = touched(touchRightPin);
  now.x = touched(touchXPin);
  now.fire = digitalRead(bumperLeftPin) == HIGH;
  now.shield = digitalRead(bumperRightPin) == LOW;

  // the first reading is the baseline: anything already active at boot
  // (or a pad whose idle value sits above the threshold) is not a press
  if( !primed ){
    last = now;
    primed = true;
  }

  pressed.up = now.up && !last.up;
  pressed.down = now.down && !last.down;
  pressed.left = now.left && !last.left;
  pressed.right = now.right && !last.right;
  pressed.x = now.x && !last.x;
  pressed.fire = now.fire && !last.fire;
  pressed.shield = now.shield && !last.shield;

  held = now;
  last = now;
}

// ----- battery -----
static float vbat = 0;
static bool usbPresent = false;
static unsigned long lastPowerSample = 0;

// resting LiPo voltage -> charge, typical curve
static const float curveVolts[] = {3.30f, 3.60f, 3.70f, 3.75f, 3.80f, 3.85f, 3.95f, 4.05f, 4.20f};
static const int curvePercent[] = {0, 5, 12, 25, 40, 55, 70, 85, 100};

static float sampleBattery(){
  uint32_t mv = 0;
  for( int i = 0; i < 8; i++ ){
    mv += analogReadMilliVolts(batterySensePin);
  }
  return mv / 8.0f / 1000.0f * batteryDivider;
}

void updatePower(){
  if( lastPowerSample != 0 && millis() - lastPowerSample < 1000 ){
    return;
  }
  float v = sampleBattery();
  // smooth out load dips from the radio, buzzer and display
  vbat = (lastPowerSample == 0) ? v : vbat * 0.8f + v * 0.2f;
  usbPresent = analogReadMilliVolts(usbSensePin) > usbPresentMv;
  lastPowerSample = millis();
}

float batteryVolts(){
  return vbat;
}

int batteryPercent(){
  const int n = sizeof(curveVolts) / sizeof(curveVolts[0]);
  if( vbat <= curveVolts[0] ) return 0;
  if( vbat >= curveVolts[n - 1] ) return 100;
  for( int i = 1; i < n; i++ ){
    if( vbat < curveVolts[i] ){
      float f = (vbat - curveVolts[i - 1]) / (curveVolts[i] - curveVolts[i - 1]);
      return curvePercent[i - 1] + f * (curvePercent[i] - curvePercent[i - 1]);
    }
  }
  return 100;
}

// The charger's CHRG/STDBY pins only drive its LED, so this is inferred
PowerState powerState(){
  if( !usbPresent ) return ON_BATTERY;
  return vbat >= batteryFullVolts ? CHARGED : CHARGING;
}

// The expander can only switch pins, so LOW lights a single colour channel
// instead of all of them. The status LED is on a real GPIO and gets PWM.
static bool rgbState[2] = {false, false};
static bool statusState = false;

static void writeRgb(int idx){
  if( !expanderOk ){
    return;
  }
  const uint8_t *pins = (idx == 0) ? led1Pins : led3Pins;
  for( int i = 0; i < 4; i++ ){
    bool lit = rgbState[idx] && (ledMode == LEDS_HIGH || (ledMode == LEDS_LOW && i == ledChannel));
    expander.digitalWrite(pins[i], lit ? LOW : HIGH);
  }
}

static void writeStatus(){
  // active low: full duty = off
  uint32_t duty = 255;
  if( statusState && ledMode == LEDS_HIGH ) duty = 0;
  if( statusState && ledMode == LEDS_LOW ) duty = 255 - statusLowDuty;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(statusLedPin, duty);
#else
  ledcWrite(channelStatusLed, duty);
#endif
}

void rgbLed(int led, bool on){
  int idx = (led == 1) ? 0 : 1;
  if( rgbState[idx] == on ){
    return;  // skip the I2C write if nothing changes
  }
  rgbState[idx] = on;
  writeRgb(idx);
}

void statusLed(bool on){
  if( statusState == on ){
    return;
  }
  statusState = on;
  writeStatus();
}

void setLedConfig(int mode, int channel){
  ledMode = constrain(mode, 0, NUM_LED_MODES - 1);
  ledChannel = constrain(channel, 0, 3);
  writeRgb(0);
  writeRgb(1);
  writeStatus();
  Preferences prefs;
  prefs.begin("cadse", false);
  prefs.putChar("ledMode", ledMode);
  prefs.putChar("ledChannel", ledChannel);
  prefs.end();
}

static const uint16_t *sfxNotes = nullptr;
static unsigned long sfxNoteEnd = 0;

void playSfx(const uint16_t *notes){
  sfxNotes = notes;
  buzzerTone(sfxNotes[0]);
  sfxNoteEnd = millis() + sfxNotes[1];
}

void updateSfx(){
  if( sfxNotes == nullptr || millis() < sfxNoteEnd ){
    return;
  }
  sfxNotes += 2;
  if( sfxNotes[1] == 0 ){
    buzzerTone(0);
    sfxNotes = nullptr;
    return;
  }
  buzzerTone(sfxNotes[0]);
  sfxNoteEnd = millis() + sfxNotes[1];
}
