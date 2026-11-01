#include "splash.h"
#include "game.h"
#include "scores.h"

#include <Preferences.h>

const char *const splashStyleNames[NUM_SPLASH_STYLES] = {"WARP", "ORBIT", "BOOT", "CIRCUIT", "OFF"};

static const unsigned long splashLengths[NUM_SPLASH_STYLES] = {3200, 3400, 3600, 3400, 0};
static const uint16_t sfxJingle[] = {392, 90, 523, 90, 659, 90, 784, 120, 0, 40, 659, 90, 1047, 260, 0, 0};
static const uint16_t sfxBeep[] = {1500, 25, 0, 0};

static int style;
static unsigned long splashSince;
static bool jinglePlayed;
static int lastBootLine;

// ---------------------------------------------------------------- saved style

static int savedStyle = -1;

int splashStyle(){
  if( savedStyle < 0 ){
    Preferences prefs;
    prefs.begin("cadse", true);
    savedStyle = constrain((int)prefs.getChar("splash", SPLASH_WARP), 0, NUM_SPLASH_STYLES - 1);
    prefs.end();
  }
  return savedStyle;
}

void setSplashStyle(int s){
  savedStyle = constrain(s, 0, NUM_SPLASH_STYLES - 1);
  Preferences prefs;
  prefs.begin("cadse", false);
  prefs.putChar("splash", savedStyle);
  prefs.end();
}

// ---------------------------------------------------------------- shared bits

static void drawSatellite(int x, int y){
  display.fillRect(x - 3, y - 3, 7, 7, SSD1306_WHITE);
  display.drawPixel(x, y, SSD1306_BLACK);
  display.drawFastHLine(x - 6, y, 3, SSD1306_WHITE);
  display.drawFastHLine(x + 4, y, 3, SSD1306_WHITE);
  display.drawRect(x - 13, y - 3, 7, 7, SSD1306_WHITE);
  display.drawRect(x + 7, y - 3, 7, 7, SSD1306_WHITE);
  display.drawFastVLine(x - 10, y - 3, 7, SSD1306_WHITE);
  display.drawFastVLine(x + 10, y - 3, 7, SSD1306_WHITE);
  display.drawFastVLine(x, y - 7, 4, SSD1306_WHITE);
}

static void drawStatusLine(int y){
  char status[28];
  snprintf(status, sizeof(status), "SD %s  IMU %s  %d%%",
           sdAvailable() ? "ok" : "--", imuAvailable() ? "ok" : "--", batteryPercent());
  display.fillRect(0, y - 1, screenW, 9, SSD1306_BLACK);
  centerText(status, y);
}

// ---------------------------------------------------------------- WARP

const int numWarpStars = 40;

struct WarpStar {
  float x, y, z;
};

static WarpStar warp[numWarpStars];

static void resetStar(WarpStar &s){
  s.x = random(-64, 65);
  s.y = random(-32, 33);
  s.z = random(10, 64);
}

static void updateWarp(unsigned long t){
  float speed = t < 900 ? 1.0f + t / 120.0f : 0.6f;
  for( int i = 0; i < numWarpStars; i++ ){
    warp[i].z -= speed;
    if( warp[i].z < 1 ) resetStar(warp[i]);
  }
  if( !jinglePlayed && t >= 1300 ){
    playSfx(sfxJingle);
    jinglePlayed = true;
  }
}

static void drawWarp(unsigned long t){
  for( int i = 0; i < numWarpStars; i++ ){
    const WarpStar &s = warp[i];
    int sx = 64 + (int)(s.x * 32 / s.z);
    int sy = 32 + (int)(s.y * 32 / s.z);
    if( t < 900 ){
      int px = 64 + (int)(s.x * 32 / (s.z + 4));
      int py = 32 + (int)(s.y * 32 / (s.z + 4));
      display.drawLine(px, py, sx, sy, SSD1306_WHITE);
    } else {
      display.drawPixel(sx, sy, SSD1306_WHITE);
    }
  }
  if( t >= 900 ){
    float f = min(1.0f, (t - 900) / 600.0f);
    f = 1 - (1 - f) * (1 - f);   // ease out
    int x = -20 + (int)(f * 84);
    display.fillRect(x - 15, 2, 31, 13, SSD1306_BLACK);
    drawSatellite(x, 8);
  }
  if( t >= 1300 ){
    display.fillRect(20, 18, 88, 18, SSD1306_BLACK);
    const char *title = "CADSE";
    for( int i = 0; i < 5; i++ ){
      long lt = (long)t - 1300 - i * 70;
      if( lt < 0 ) continue;
      display.setTextSize(3);
      display.setCursor(20 + i * 18, 20 - max(0L, 12 - lt / 10));
      display.print(title[i]);
    }
  }
  if( t >= 1800 ){
    display.fillRect(30, 43, 68, 9, SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(31, 44);
    display.print("A R C A D E");
  }
  if( t >= 2200 ){
    drawStatusLine(56);
  }
}

// ---------------------------------------------------------------- ORBIT

static void drawOrbit(unsigned long t){
  // a few fixed stars
  static const uint8_t starXY[][2] = {{5, 4}, {120, 8}, {70, 3}, {100, 30}, {12, 40}, {124, 50}, {40, 12}, {88, 58}};
  for( auto &s : starXY ) display.drawPixel(s[0], s[1], SSD1306_WHITE);

  // planet with a ring of latitude lines
  const int px = 24, py = 32, pr = 13;
  display.fillCircle(px, py, pr, SSD1306_WHITE);
  display.drawFastHLine(px - 11, py - 5, 18, SSD1306_BLACK);
  display.drawFastHLine(px - 12, py + 2, 22, SSD1306_BLACK);
  display.drawFastHLine(px - 9, py + 8, 14, SSD1306_BLACK);

  // satellite on an ellipse, hidden while behind the planet
  float a = t / 450.0f;
  int sx = px + (int)(cosf(a) * 21);
  int sy = py + (int)(sinf(a) * 8);
  bool behind = sinf(a) < 0 && abs(sx - px) < pr;
  if( !behind ){
    display.fillRect(sx - 1, sy - 1, 3, 3, SSD1306_WHITE);
    display.drawFastHLine(sx - 4, sy, 2, SSD1306_WHITE);
    display.drawFastHLine(sx + 3, sy, 2, SSD1306_WHITE);
  }

  // title types itself out with a blinking cursor
  const char *title = "CADSE";
  int chars = constrain(((long)t - 600) / 150, 0L, 5L);
  display.setTextSize(2);
  display.setCursor(56, 14);
  for( int i = 0; i < chars; i++ ) display.print(title[i]);
  if( chars < 5 || (millis() / 300) % 2 ){
    display.fillRect(56 + chars * 12, 14, 10, 14, (chars < 5) ? SSD1306_WHITE : SSD1306_BLACK);
  }
  display.setTextSize(1);
  if( t > 1500 ){
    display.setCursor(57, 34);
    display.print("ARCADE");
  }
  if( t > 2200 ){
    drawStatusLine(56);
  }
}

static void updateOrbit(unsigned long t){
  int chars = constrain(((long)t - 600) / 150, 0L, 5L);
  static int lastChars = 0;
  if( t < 100 ) lastChars = 0;
  if( chars > lastChars ){
    lastChars = chars;
    playSfx(chars == 5 ? sfxJingle : sfxBeep);
  }
}

// ---------------------------------------------------------------- BOOT

const int numBootLines = 6;

static void bootLine(int i, char *buf, size_t len){
  switch( i ){
    case 0: snprintf(buf, len, "OLED 128x64 .. ok"); break;
    case 1: snprintf(buf, len, "IMU LSM6DS3 .. %s", imuAvailable() ? "ok" : "--"); break;
    case 2: snprintf(buf, len, "SD CARD ...... %s", sdAvailable() ? "ok" : "--"); break;
    case 3: snprintf(buf, len, "TOUCH x5 ..... ok"); break;
    case 4: snprintf(buf, len, "RADIO ........ ok"); break;
    default: snprintf(buf, len, "BATTERY ...... %d%%", batteryPercent()); break;
  }
}

static void updateBoot(unsigned long t){
  int lines = constrain(((long)t - 400) / 300, 0L, (long)numBootLines);
  if( t < 100 ) lastBootLine = 0;
  if( lines > lastBootLine ){
    lastBootLine = lines;
    playSfx(sfxBeep);
  }
  if( !jinglePlayed && t >= 2700 ){
    playSfx(sfxJingle);
    jinglePlayed = true;
  }
}

static void drawBoot(unsigned long t){
  display.setTextSize(1);
  if( t < 2700 ){
    display.setCursor(0, 0);
    display.print("CADSE BIOS v1.0");
    int lines = constrain(((long)t - 400) / 300, 0L, (long)numBootLines);
    char buf[24];
    for( int i = 0; i < lines; i++ ){
      bootLine(i, buf, sizeof(buf));
      display.setCursor(0, 10 + i * 8);
      display.print(buf);
    }
    if( lines < numBootLines && (millis() / 200) % 2 ){
      display.fillRect(0, 10 + lines * 8, 5, 7, SSD1306_WHITE);
    }
    // progress bar along the bottom
    int w = constrain((int)(t * 124 / 2600), 0, 124);
    display.drawRect(0, 59, 128, 5, SSD1306_WHITE);
    display.fillRect(2, 61, w, 1, SSD1306_WHITE);
  } else {
    // flash to the logo
    display.drawRect(8, 10, 112, 40, SSD1306_WHITE);
    centerText("CADSE", 16, 3);
    centerText("ARCADE  READY", 40);
    if( (millis() / 400) % 2 ){
      centerText("_", 54);
    }
  }
}

// ---------------------------------------------------------------- CIRCUIT

struct Trace {
  int8_t x0, y0, xm, ym, x1, y1;   // start at the edge, bend, end at the logo
};

static const Trace traces[] = {
  {0, 8, 20, 8, 30, 20},     {0, 56, 16, 56, 30, 44},
  {127, 6, 104, 6, 97, 20},  {127, 58, 110, 58, 97, 44},
  {40, 0, 40, 10, 44, 20},   {84, 0, 84, 10, 80, 20},
  {52, 63, 52, 54, 56, 44},  {74, 63, 74, 54, 70, 44},
  {0, 32, 12, 32, 30, 32},   {127, 32, 115, 32, 97, 32},
};
const int numTraces = sizeof(traces) / sizeof(traces[0]);

static void drawPartialLine(int x0, int y0, int x1, int y1, float f){
  if( f <= 0 ) return;
  f = min(f, 1.0f);
  display.drawLine(x0, y0, x0 + (int)((x1 - x0) * f), y0 + (int)((y1 - y0) * f), SSD1306_WHITE);
}

static void drawCircuit(unsigned long t){
  // traces grow in two segments, then end in a pad
  for( int i = 0; i < numTraces; i++ ){
    const Trace &tr = traces[i];
    float f = ((long)t - i * 60) / 700.0f;
    drawPartialLine(tr.x0, tr.y0, tr.xm, tr.ym, f * 2);
    drawPartialLine(tr.xm, tr.ym, tr.x1, tr.y1, f * 2 - 1);
    display.fillCircle(tr.x0, tr.y0, 1, SSD1306_WHITE);
    if( f >= 1 ) display.drawCircle(tr.x1, tr.y1, 2, SSD1306_WHITE);
  }
  // the "chip" with the logo lights up once every trace has arrived
  if( t > 1300 ){
    bool lit = t > 1500;
    display.fillRect(30, 20, 68, 25, lit ? SSD1306_WHITE : SSD1306_BLACK);
    display.drawRect(30, 20, 68, 25, SSD1306_WHITE);
    display.setTextColor(lit ? SSD1306_BLACK : SSD1306_WHITE);
    centerText("CADSE", 25, 2);
    display.setTextSize(1);
    centerText("ARCADE", 36);
    display.setTextColor(SSD1306_WHITE);
  }
  if( t > 2200 ){
    drawStatusLine(56);
  }
}

static void updateCircuit(unsigned long t){
  if( !jinglePlayed && t >= 1500 ){
    playSfx(sfxJingle);
    jinglePlayed = true;
  }
}

// ---------------------------------------------------------------- interface

void splashStart(int s){
  style = constrain(s, 0, NUM_SPLASH_STYLES - 1);
  for( int i = 0; i < numWarpStars; i++ ){
    resetStar(warp[i]);
  }
  splashSince = millis();
  jinglePlayed = false;
  lastBootLine = 0;
}

void splashStart(){
  splashStart(splashStyle());
}

bool splashUpdate(const Buttons &pressed){
  unsigned long t = millis() - splashSince;
  switch( style ){
    case SPLASH_WARP:    updateWarp(t); break;
    case SPLASH_ORBIT:   updateOrbit(t); break;
    case SPLASH_BOOT:    updateBoot(t); break;
    case SPLASH_CIRCUIT: updateCircuit(t); break;
  }
  rgbLed(1, jinglePlayed && t % 600 < 150);
  rgbLed(3, jinglePlayed && (t + 300) % 600 < 150);

  bool anyPressed = pressed.x || pressed.fire || pressed.shield || pressed.up || pressed.down || pressed.left || pressed.right;
  bool skip = t > 600 && anyPressed;   // ignore stray presses right after boot
  if( skip || t >= splashLengths[style] ){
    rgbLed(1, false);
    rgbLed(3, false);
    return false;
  }
  return true;
}

void splashDraw(){
  unsigned long t = millis() - splashSince;
  display.clearDisplay();
  display.invertDisplay(false);
  display.setTextColor(SSD1306_WHITE);
  switch( style ){
    case SPLASH_WARP:    drawWarp(t); break;
    case SPLASH_ORBIT:   drawOrbit(t); break;
    case SPLASH_BOOT:    drawBoot(t); break;
    case SPLASH_CIRCUIT: drawCircuit(t); break;
  }
  display.setTextSize(1);
  display.display();
}
