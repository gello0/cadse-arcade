/* SENSORS - live dashboard for the CADSE board
 *
 * Pages (touch Left/Right):
 *   ENVIRONMENT  BME280 temperature, humidity, pressure, altitude + trend
 *   GRAPH        scrolling plot of one value, touch Up/Down picks it
 *   MOTION       spirit level, pitch/roll, gyro rates (LSM6DS3)
 *   ANALOG       ADS1115 inputs A_IN0..3
 *   SYSTEM       battery, USB, uptime, memory, board id
 * Left bumper starts/stops logging everything to /sensors/log_N.csv once per
 * second. Touch X goes back to the menu.
 */
#include "dashboard.h"
#include "game.h"
#include "scores.h"

#include <Wire.h>
#include <WiFi.h>
#include <SD_MMC.h>
#include <Adafruit_BME280.h>
#include <Adafruit_ADS1X15.h>

const uint8_t bmeAddress = 0x76;
const uint8_t adsAddress = 0x48;
const float seaLevelHpa = 1013.25f;
const unsigned long sampleEveryMs = 500;     // history and graph rate
const unsigned long logEveryMs = 1000;
const unsigned long analogEveryMs = 250;
const int historyLen = 120;                  // 60 s at 2 Hz

enum Page { PAGE_ENV, PAGE_GRAPH, PAGE_MOTION, PAGE_ANALOG, PAGE_SYSTEM, NUM_PAGES };
static const char *pageNames[NUM_PAGES] = {"ENVIRONMENT", "GRAPH", "MOTION", "ANALOG", "SYSTEM"};

enum Quantity { Q_TEMP, Q_HUM, Q_PRESS, Q_ACCEL, Q_BATT, Q_AIN0, NUM_QUANTITIES };
static const char *quantityNames[NUM_QUANTITIES] = {"temp C", "humidity %", "pressure hPa", "accel m/s2", "battery V", "A_IN0 V"};

static Adafruit_BME280 bme;
static Adafruit_ADS1115 ads;
static bool bmeOk = false, adsOk = false, sensorsStarted = false;

static int page;
static int graphQuantity;

// latest readings
static float tempC, humidity, pressureHpa, altitudeM;
static float accel[3], gyro[3], imuTempC;
static float ain[4];

static float history[NUM_QUANTITIES][historyLen];
static int historyCount, historyPos;
static unsigned long lastSample, lastAnalog, lastLog;

static bool logging;
static char logPath[24];
static int logLines;

// ---------------------------------------------------------------- sampling

static void readEnvironment(){
  if( !bmeOk ) return;
  tempC = bme.readTemperature();
  humidity = bme.readHumidity();
  pressureHpa = bme.readPressure() / 100.0f;
  altitudeM = bme.readAltitude(seaLevelHpa);
}

static void readAnalog(){
  if( !adsOk ) return;
  for( int ch = 0; ch < 4; ch++ ){
    ain[ch] = ads.computeVolts(ads.readADC_SingleEnded(ch));
  }
}

static float accelMagnitude(){
  return sqrtf(accel[0] * accel[0] + accel[1] * accel[1] + accel[2] * accel[2]);
}

static void pushHistory(){
  float values[NUM_QUANTITIES] = {tempC, humidity, pressureHpa, accelMagnitude(), batteryVolts(), ain[0]};
  for( int q = 0; q < NUM_QUANTITIES; q++ ){
    history[q][historyPos] = values[q];
  }
  historyPos = (historyPos + 1) % historyLen;
  if( historyCount < historyLen ) historyCount++;
}

// ---------------------------------------------------------------- logging

static void startLogging(){
  if( !sdAvailable() ){
    playSfx(sfxHit);
    return;
  }
  if( !SD_MMC.exists("/sensors") ){
    SD_MMC.mkdir("/sensors");
  }
  for( int n = 1; n < 1000; n++ ){
    snprintf(logPath, sizeof(logPath), "/sensors/log_%d.csv", n);
    if( !SD_MMC.exists(logPath) ) break;
  }
  File f = SD_MMC.open(logPath, FILE_WRITE);
  if( !f ){
    playSfx(sfxHit);
    return;
  }
  f.println("ms,temp_c,humidity_pct,pressure_hpa,altitude_m,ax,ay,az,gx,gy,gz,"
            "ain0_v,ain1_v,ain2_v,ain3_v,battery_v,battery_pct,usb");
  f.close();
  logging = true;
  logLines = 0;
  lastLog = 0;
  playSfx(sfxPickup);
}

static void writeLogLine(){
  File f = SD_MMC.open(logPath, FILE_APPEND);
  if( !f ){
    logging = false;
    playSfx(sfxHit);
    return;
  }
  f.printf("%lu,%.2f,%.2f,%.2f,%.1f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%d,%d\n",
           millis(), tempC, humidity, pressureHpa, altitudeM,
           accel[0], accel[1], accel[2], gyro[0], gyro[1], gyro[2],
           ain[0], ain[1], ain[2], ain[3], batteryVolts(), batteryPercent(),
           powerState() != ON_BATTERY);
  f.close();
  logLines++;
}

// ---------------------------------------------------------------- interface

void dashboardStart(){
  if( !sensorsStarted ){
    sensorsStarted = true;
    bmeOk = bme.begin(bmeAddress, &Wire);
    adsOk = ads.begin(adsAddress, &Wire);
    if( adsOk ){
      ads.setGain(GAIN_TWOTHIRDS);              // +-6.144 V range
      ads.setDataRate(RATE_ADS1115_860SPS);     // fast single reads, ~1 ms each
    }
    Serial.printf("BME280 %s, ADS1115 %s\n", bmeOk ? "ok" : "missing", adsOk ? "ok" : "missing");
  }
  page = PAGE_ENV;
  graphQuantity = Q_TEMP;
  historyCount = historyPos = 0;
  lastSample = lastAnalog = 0;
  logging = false;
  readEnvironment();
  readAnalog();
}

bool dashboardUpdate(const Buttons &held, const Buttons &pressed){
  unsigned long now = millis();

  if( pressed.left ){ page = (page + NUM_PAGES - 1) % NUM_PAGES; playSfx(sfxClick); }
  if( pressed.right ){ page = (page + 1) % NUM_PAGES; playSfx(sfxClick); }
  if( page == PAGE_GRAPH ){
    if( pressed.up ){ graphQuantity = (graphQuantity + NUM_QUANTITIES - 1) % NUM_QUANTITIES; playSfx(sfxClick); }
    if( pressed.down ){ graphQuantity = (graphQuantity + 1) % NUM_QUANTITIES; playSfx(sfxClick); }
  }
  if( pressed.fire ){
    if( logging ){
      logging = false;
      playSfx(sfxClick);
    } else {
      startLogging();
    }
  }
  if( pressed.x ){
    logging = false;
    rgbLed(3, false);
    return false;
  }

  readImu(accel, gyro, imuTempC);
  if( now - lastAnalog >= analogEveryMs ){
    lastAnalog = now;
    readAnalog();
  }
  if( now - lastSample >= sampleEveryMs ){
    lastSample = now;
    readEnvironment();
    pushHistory();
  }
  if( logging && now - lastLog >= logEveryMs ){
    lastLog = now;
    writeLogLine();
  }
  rgbLed(3, logging && (now / 500) % 2);
  return true;
}

// ---------------------------------------------------------------- drawing

static float historyAt(int q, int i){   // i = 0 is the oldest sample
  int start = (historyPos - historyCount + historyLen) % historyLen;
  return history[q][(start + i) % historyLen];
}

static void drawPlot(int q, int x, int y, int w, int h, bool labels){
  if( historyCount < 2 ){
    centerText("collecting...", y + h / 2 - 3);
    return;
  }
  float lo = 1e9, hi = -1e9;
  for( int i = 0; i < historyCount; i++ ){
    float v = historyAt(q, i);
    lo = min(lo, v);
    hi = max(hi, v);
  }
  // keep a minimum span so noise doesn't fill the whole plot
  static const float minSpan[NUM_QUANTITIES] = {0.5f, 2.0f, 0.5f, 0.5f, 0.05f, 0.05f};
  if( hi - lo < minSpan[q] ){
    float mid = (hi + lo) / 2;
    lo = mid - minSpan[q] / 2;
    hi = mid + minSpan[q] / 2;
  }
  display.drawRect(x, y, w, h, SSD1306_WHITE);
  int n = min(historyCount, w - 2);
  int prevPx = -1, prevPy = -1;
  for( int i = 0; i < n; i++ ){
    float v = historyAt(q, historyCount - n + i);
    int px = x + 1 + i * (w - 3) / max(1, n - 1);
    int py = y + h - 2 - (int)((v - lo) / (hi - lo) * (h - 3));
    if( prevPx >= 0 ){
      display.drawLine(prevPx, prevPy, px, py, SSD1306_WHITE);
    }
    prevPx = px;
    prevPy = py;
  }
  if( labels ){
    char buf[12];
    snprintf(buf, sizeof(buf), "%.2f", hi);
    display.setCursor(x + 2, y + 2);
    display.print(buf);
    snprintf(buf, sizeof(buf), "%.2f", lo);
    display.setCursor(x + 2, y + h - 9);
    display.print(buf);
  }
}

static void drawTitle(){
  display.fillRect(0, 0, screenW, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(pageNames[page]);
  if( logging && (millis() / 500) % 2 ){
    display.setCursor(80, 1);
    display.print("REC");
  }
  // page dots
  for( int i = 0; i < NUM_PAGES; i++ ){
    int dx = screenW - 4 - (NUM_PAGES - 1 - i) * 5;
    if( i == page ) display.fillCircle(dx, 4, 2, SSD1306_BLACK);
    else display.drawPixel(dx, 4, SSD1306_BLACK);
  }
  display.setTextColor(SSD1306_WHITE);
}

static void drawEnvironment(){
  if( !bmeOk ){
    centerText("BME280 not found", 28);
    return;
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", tempC);
  display.setTextSize(2);
  display.setCursor(2, 14);
  display.print(buf);
  display.setTextSize(1);
  display.setCursor(2 + strlen(buf) * 12 + 2, 14);
  display.print("o");
  display.setCursor(2 + strlen(buf) * 12 + 8, 21);
  display.print("C");

  display.setCursor(2, 34);
  display.printf("hum  %.0f %%", humidity);
  display.setCursor(2, 44);
  display.printf("pres %.1f", pressureHpa);
  display.setCursor(2, 54);
  display.printf("alt  %.0f m", altitudeM);

  drawPlot(Q_TEMP, 76, 13, 52, 50, false);
}

static void drawGraph(){
  display.setCursor(2, 13);
  display.print(quantityNames[graphQuantity]);
  float v = historyCount ? historyAt(graphQuantity, historyCount - 1) : 0;
  char buf[12];
  snprintf(buf, sizeof(buf), "%.2f", v);
  display.setCursor(screenW - 2 - strlen(buf) * 6, 13);
  display.print(buf);
  drawPlot(graphQuantity, 0, 22, screenW, 42, true);
}

static void drawMotion(){
  if( !imuAvailable() ){
    centerText("IMU not found", 28);
    return;
  }
  // spirit level: the bubble floats to the high side
  const int cx = 32, cy = 37, r = 24;
  display.drawCircle(cx, cy, r, SSD1306_WHITE);
  display.drawCircle(cx, cy, 5, SSD1306_WHITE);
  display.drawFastHLine(cx - r, cy, 2 * r + 1, SSD1306_WHITE);
  display.drawFastVLine(cx, cy - r, 2 * r + 1, SSD1306_WHITE);
  float tx, ty;
  readTilt2(tx, ty);
  float bx = -tx / 9.81f * r, by = -ty / 9.81f * r;
  float len = sqrtf(bx * bx + by * by);
  if( len > r - 4 ){
    bx *= (r - 4) / len;
    by *= (r - 4) / len;
  }
  display.fillCircle(cx + (int)bx, cy + (int)by, 3, SSD1306_WHITE);

  float pitch = atan2f(-accel[0], sqrtf(accel[1] * accel[1] + accel[2] * accel[2])) * 57.296f;
  float roll = atan2f(accel[1], accel[2]) * 57.296f;
  display.setCursor(64, 13);
  display.printf("pitch%5.1f", pitch);
  display.setCursor(64, 22);
  display.printf("roll %5.1f", roll);
  display.setCursor(64, 34);
  display.printf("gx %6.1f", gyro[0] * 57.296f);
  display.setCursor(64, 43);
  display.printf("gy %6.1f", gyro[1] * 57.296f);
  display.setCursor(64, 52);
  display.printf("gz %6.1f", gyro[2] * 57.296f);
}

static void drawAnalog(){
  if( !adsOk ){
    centerText("ADS1115 not found", 28);
    return;
  }
  for( int ch = 0; ch < 4; ch++ ){
    int y = 14 + ch * 12;
    display.setCursor(0, y);
    display.printf("A%d %6.3fV", ch, ain[ch]);
    // bar for 0 .. 3.3 V
    display.drawRect(72, y, 54, 7, SSD1306_WHITE);
    int w = constrain((int)(ain[ch] / 3.3f * 52), 0, 52);
    display.fillRect(73, y + 1, w, 5, SSD1306_WHITE);
  }
}

static void drawSystem(){
  const char *state = powerState() == CHARGING ? "charging" : powerState() == CHARGED ? "charged" : "on battery";
  display.setCursor(0, 13);
  display.printf("bat %.2fV %d%%", batteryVolts(), batteryPercent());
  display.setCursor(0, 22);
  display.printf("pwr %s", state);
  unsigned long s = millis() / 1000;
  display.setCursor(0, 31);
  display.printf("up  %luh %02lum %02lus", s / 3600, (s / 60) % 60, s % 60);
  display.setCursor(0, 40);
  display.printf("ram %u kB free", (unsigned)(ESP.getFreeHeap() / 1024));
  display.setCursor(0, 49);
  display.printf("imu %.1f C  sd %s", imuTempC, sdAvailable() ? "ok" : "--");
  display.setCursor(0, 57);
  display.printf("mac %s", WiFi.macAddress().substring(9).c_str());
}

void dashboardDraw(){
  display.clearDisplay();
  display.setTextSize(1);
  drawTitle();
  switch( page ){
    case PAGE_ENV:    drawEnvironment(); break;
    case PAGE_GRAPH:  drawGraph(); break;
    case PAGE_MOTION: drawMotion(); break;
    case PAGE_ANALOG: drawAnalog(); break;
    case PAGE_SYSTEM: drawSystem(); break;
  }
  display.display();
}
