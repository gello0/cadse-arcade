/* CAVE THRUSTER
 *
 * Fly a lander through an endless, narrowing cave. Gravity pulls you down,
 * thrust pushes you up. Grab fuel crystals for bonus points.
 *   Hold either bumper or touch Up : thrust
 * The cave scrolls faster and the gap closes the further you get, and rock
 * pillars start to appear.
 */
#include "../game.h"

const int caveHudH = 9;
const int caveShipX = 22;
const int maxPillars = 3;

const float caveGravity = 0.11f;
const float caveThrust = 0.24f;
const float caveMaxVY = 2.4f;

struct Pillar {
  bool active;
  float x;
  int top, bottom;   // solid between these rows
};

static uint8_t caveTop[screenW], caveBottom[screenW];
static float caveCenter, caveGap;
static float scrollAcc;
static float shipY, shipVY;
static bool thrusting;
static unsigned long distance;
static uint32_t bonus;
static int crystals;
static Pillar pillars[maxPillars];
static bool crystalActive;
static float crystalX;
static int crystalY;
static unsigned long nextPillarAt;

static void newColumn(uint8_t &top, uint8_t &bottom){
  caveCenter += random(-2, 3) * 0.7f;
  float minGap = 18;
  caveGap = max(minGap, 44.0f - distance / 150.0f);
  float half = caveGap / 2;
  caveCenter = constrain(caveCenter, caveHudH + 1 + half, screenH - 1 - half);
  top = (uint8_t)(caveCenter - half);
  bottom = (uint8_t)(caveCenter + half);
}

static void caveStart(){
  caveCenter = (caveHudH + screenH) / 2;
  distance = 0;
  for( int x = 0; x < screenW; x++ ){
    newColumn(caveTop[x], caveBottom[x]);
  }
  scrollAcc = 0;
  shipY = caveCenter;
  shipVY = 0;
  bonus = 0;
  crystals = 0;
  crystalActive = false;
  nextPillarAt = 400;
  for( int i = 0; i < maxPillars; i++ ){
    pillars[i].active = false;
  }
}

static void scrollOne(){
  memmove(caveTop, caveTop + 1, screenW - 1);
  memmove(caveBottom, caveBottom + 1, screenW - 1);
  newColumn(caveTop[screenW - 1], caveBottom[screenW - 1]);
  distance++;

  for( int i = 0; i < maxPillars; i++ ){
    if( pillars[i].active ){
      pillars[i].x -= 1;
      if( pillars[i].x < -4 ) pillars[i].active = false;
    }
  }
  if( crystalActive ){
    crystalX -= 1;
    if( crystalX < -3 ) crystalActive = false;
  }

  // a pillar grows from the ceiling or floor, leaving a passage
  if( distance >= nextPillarAt ){
    nextPillarAt = distance + random(70, 130);
    for( int i = 0; i < maxPillars; i++ ){
      if( !pillars[i].active ){
        int top = caveTop[screenW - 1], bottom = caveBottom[screenW - 1];
        int len = (bottom - top) / 2 - 2;
        pillars[i].active = true;
        pillars[i].x = screenW;
        if( random(2) ){
          pillars[i].top = top;
          pillars[i].bottom = top + len;
        } else {
          pillars[i].top = bottom - len;
          pillars[i].bottom = bottom;
        }
        break;
      }
    }
  } else if( !crystalActive && random(160) == 0 ){
    crystalActive = true;
    crystalX = screenW;
    crystalY = (caveTop[screenW - 1] + caveBottom[screenW - 1]) / 2;
  }
}

static bool caveUpdate(const Buttons &held, const Buttons &pressed){
  thrusting = held.fire || held.shield || held.up;
  shipVY += thrusting ? -caveThrust : caveGravity;
  shipVY = constrain(shipVY, -caveMaxVY, caveMaxVY);
  shipY += shipVY;

  float speed = min(3.0f, 1.0f + distance / 900.0f);
  scrollAcc += speed;
  while( scrollAcc >= 1 ){
    scrollAcc -= 1;
    scrollOne();
  }

  // the ship is a 7x5 box around caveShipX, shipY
  int y0 = (int)shipY - 2, y1 = (int)shipY + 2;
  for( int x = caveShipX - 3; x <= caveShipX + 3; x++ ){
    if( y0 <= caveTop[x] || y1 >= caveBottom[x] ){
      playSfx(sfxHit);
      return false;
    }
  }
  for( int i = 0; i < maxPillars; i++ ){
    const Pillar &p = pillars[i];
    if( p.active && p.x - 3 <= caveShipX + 3 && p.x + 3 >= caveShipX - 3 && y1 >= p.top && y0 <= p.bottom ){
      playSfx(sfxHit);
      return false;
    }
  }
  if( crystalActive && fabsf(crystalX - caveShipX) < 5 && abs(crystalY - (int)shipY) < 5 ){
    crystalActive = false;
    crystals++;
    bonus += 25;
    playSfx(sfxPickup);
  }

  rgbLed(3, thrusting);
  return true;
}

static uint32_t caveScore(){
  return distance / 4 + bonus;
}

static void caveDraw(){
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(caveScore());
  display.setCursor(80, 0);
  display.printf("%4lum", distance / 10);
  display.drawFastHLine(0, caveHudH - 1, screenW, SSD1306_WHITE);

  for( int x = 0; x < screenW; x++ ){
    display.drawFastVLine(x, caveHudH, caveTop[x] - caveHudH + 1, SSD1306_WHITE);
    display.drawFastVLine(x, caveBottom[x], screenH - caveBottom[x], SSD1306_WHITE);
  }
  for( int i = 0; i < maxPillars; i++ ){
    const Pillar &p = pillars[i];
    if( p.active ){
      display.fillRect((int)p.x - 3, p.top, 7, p.bottom - p.top + 1, SSD1306_WHITE);
    }
  }
  if( crystalActive ){
    int cx = (int)crystalX;
    display.drawLine(cx, crystalY - 3, cx + 2, crystalY, SSD1306_WHITE);
    display.drawLine(cx + 2, crystalY, cx, crystalY + 3, SSD1306_WHITE);
    display.drawLine(cx, crystalY + 3, cx - 2, crystalY, SSD1306_WHITE);
    display.drawLine(cx - 2, crystalY, cx, crystalY - 3, SSD1306_WHITE);
  }

  int sy = (int)shipY;
  display.fillTriangle(caveShipX - 3, sy - 2, caveShipX - 3, sy + 2, caveShipX + 3, sy, SSD1306_WHITE);
  if( thrusting && (millis() / 60) % 2 ){
    display.drawFastHLine(caveShipX - 7, sy, 3, SSD1306_WHITE);
    display.drawPixel(caveShipX - 5, sy - 1, SSD1306_WHITE);
    display.drawPixel(caveShipX - 5, sy + 1, SSD1306_WHITE);
  }
}

static void caveSummary(char *buf, size_t len){
  snprintf(buf, len, "%lum crystals %d", distance / 10, crystals);
}

extern const Game caveGame = {
  "CAVE THRUSTER", "cave", "hold bumper to fly",
  caveStart, caveUpdate, caveDraw, caveScore, caveSummary, nullptr,
};
