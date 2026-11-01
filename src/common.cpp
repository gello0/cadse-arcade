#include "game.h"

// ----- sound effects {freq, ms}, ended by {0, 0} -----
const uint16_t sfxClick[] = {2000, 15, 0, 0};
const uint16_t sfxBlip[] = {1200, 20, 0, 0};
const uint16_t sfxPickup[] = {1047, 50, 1319, 50, 1568, 80, 0, 0};
const uint16_t sfxBoom[] = {300, 40, 180, 60, 90, 80, 0, 0};
const uint16_t sfxHit[] = {200, 90, 0, 40, 150, 160, 0, 0};
const uint16_t sfxLevel[] = {784, 70, 988, 70, 1175, 70, 1568, 140, 0, 0};
const uint16_t sfxStart[] = {523, 80, 659, 80, 784, 80, 1047, 160, 0, 0};
const uint16_t sfxGameOver[] = {784, 150, 659, 150, 523, 150, 392, 400, 0, 0};

// ----- game clock -----
static unsigned long clockOffset = 0;
static unsigned long pauseStart = 0;
static bool clockPaused = false;

void gameClockReset(){
  clockOffset = millis();
  clockPaused = false;
}

void gameClockPause(){
  if( !clockPaused ){
    pauseStart = millis();
    clockPaused = true;
  }
}

void gameClockResume(){
  if( clockPaused ){
    clockOffset += millis() - pauseStart;
    clockPaused = false;
  }
}

unsigned long gameMillis(){
  return (clockPaused ? pauseStart : millis()) - clockOffset;
}

// ----- drawing -----
void centerText(const char *text, int y, int size){
  display.setTextSize(size);
  int w = strlen(text) * 6 * size;
  display.setCursor((screenW - w) / 2, y);
  display.print(text);
}

void drawBatteryIcon(int x, int y, uint16_t color){
  PowerState ps = powerState();
  int pct = batteryPercent();
  if( ps != ON_BATTERY ){
    // small lightning bolt left of the battery
    display.drawLine(x + 3, y, x + 1, y + 3, color);
    display.drawLine(x + 1, y + 3, x + 4, y + 3, color);
    display.drawLine(x + 4, y + 3, x + 2, y + 6, color);
  }
  // blink the outline when nearly empty
  if( ps == ON_BATTERY && pct < 10 && (millis() / 400) % 2 ){
    return;
  }
  int bx = x + 6;
  display.drawRect(bx, y, 12, 7, color);
  display.drawFastVLine(bx + 12, y + 2, 3, color);
  int fill = (ps == CHARGING) ? ((millis() / 400) % 11) : (pct * 10 + 50) / 100;  // charging animates
  if( fill > 0 ){
    display.fillRect(bx + 1, y + 1, min(fill, 10), 5, color);
  }
}

const int numStars = 24;

struct Star {
  float x, y, speed;
};

static Star stars[numStars];
static int starTop = 0;

void initStars(){
  for( int i = 0; i < numStars; i++ ){
    stars[i].x = random(screenW);
    stars[i].y = random(screenH);
    stars[i].speed = 0.2f + random(100) / 100.0f;
  }
}

void updateStars(float speed, int top){
  starTop = top;
  for( int i = 0; i < numStars; i++ ){
    stars[i].y += stars[i].speed * speed;
    if( stars[i].y >= screenH ){
      stars[i].y = top;
      stars[i].x = random(screenW);
    }
  }
}

void drawStars(){
  for( int i = 0; i < numStars; i++ ){
    if( stars[i].y >= starTop ){
      display.drawPixel((int)stars[i].x, (int)stars[i].y, SSD1306_WHITE);
    }
  }
}
