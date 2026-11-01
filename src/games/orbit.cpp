/* ORBIT DODGER
 *
 * Tilt the board to steer your satellite through falling space debris.
 *   Tilt / touch Left+Right : steer
 *   Left bumper             : laser, destroys the lowest debris above you
 *   Right bumper (hold)     : shield, drains energy
 * Solar cells give points and recharge the shield. Every 15 s the debris
 * gets faster and denser.
 */
#include "../game.h"

const int hudH = 9;
const int shipY = 57;
const int shipHalfW = 7;
const int shipHalfH = 3;
const int maxDebris = 14;

const float tiltGain = 0.9f;               // px per frame per m/s^2
const float touchSpeed = 2.5f;
const unsigned long laserCooldownMs = 450;
const unsigned long laserVisibleMs = 90;
const float shieldDrainPerFrame = 1.6f;
const float shieldRechargePerFrame = 0.12f;
const unsigned long invulnerableMs = 1500;
const unsigned long levelDurationMs = 15000;

static const uint16_t sfxLaser[] = {1800, 25, 1200, 25, 700, 25, 0, 0};

struct Debris {
  bool active;
  bool isCell;       // solar cell pickup instead of debris
  float x, y, vy, vx;
  int r;
  uint8_t shape;
};

static Debris debris[maxDebris];
static float shipX;
static float tiltZero;
static int lives;
static float shield;
static bool shieldOn;
static uint32_t score;
static int level;
static unsigned long nextSpawn, lastLaser, invulnerableUntil, flashUntil;
static int laserX;
static int debrisShot, cellsCollected;

static void drawShip(int x, int y){
  display.fillRect(x - 2, y - 3, 5, 6, SSD1306_WHITE);        // bus
  display.drawFastHLine(x - 4, y, 2, SSD1306_WHITE);          // panel arms
  display.drawFastHLine(x + 3, y, 2, SSD1306_WHITE);
  display.drawRect(x - 8, y - 2, 4, 5, SSD1306_WHITE);        // solar panels
  display.drawRect(x + 5, y - 2, 4, 5, SSD1306_WHITE);
  display.drawPixel(x, y - 5, SSD1306_WHITE);                 // antenna
  display.drawPixel(x, y - 4, SSD1306_WHITE);
}

static void drawDebris(const Debris &d){
  int x = (int)d.x, y = (int)d.y;
  if( d.isCell ){
    // blinking solar cell with a grid
    if( (millis() / 150) % 2 ){
      display.drawRect(x - 3, y - 3, 7, 7, SSD1306_WHITE);
      display.drawFastHLine(x - 3, y, 7, SSD1306_WHITE);
      display.drawFastVLine(x, y - 3, 7, SSD1306_WHITE);
    } else {
      display.fillRect(x - 3, y - 3, 7, 7, SSD1306_WHITE);
    }
    return;
  }
  switch( d.shape ){
    case 0:
      display.fillCircle(x, y, d.r, SSD1306_WHITE);
      display.drawPixel(x - 1, y - 1, SSD1306_BLACK);  // crater
      break;
    case 1:
      display.drawCircle(x, y, d.r, SSD1306_WHITE);
      display.drawPixel(x, y, SSD1306_WHITE);
      break;
    default:
      display.fillTriangle(x - d.r, y + d.r, x + d.r, y + d.r - 1, x, y - d.r, SSD1306_WHITE);
      break;
  }
}

static void spawn(){
  for( int i = 0; i < maxDebris; i++ ){
    Debris &d = debris[i];
    if( d.active ){
      continue;
    }
    d.active = true;
    d.isCell = random(100) < 8;
    d.r = d.isCell ? 3 : random(2, 5 + min(level, 3));
    d.x = random(d.r + 2, screenW - d.r - 2);
    d.y = hudH - d.r;
    d.vy = 0.7f + level * 0.18f + random(60) / 100.0f;
    d.vx = (level >= 3 && !d.isCell) ? (random(-40, 41) / 100.0f) : 0;
    d.shape = random(3);
    return;
  }
}

static void orbitStart(){
  for( int i = 0; i < maxDebris; i++ ){
    debris[i].active = false;
  }
  shipX = screenW / 2;
  tiltZero = readTilt();   // whatever angle you hold the board at is "level"
  lives = 3;
  shield = 100;
  shieldOn = false;
  score = 0;
  level = 1;
  debrisShot = 0;
  cellsCollected = 0;
  nextSpawn = 800;
  lastLaser = 0;
  flashUntil = 0;
  invulnerableUntil = invulnerableMs;
  laserX = -1;
}

static void orbitResume(){
  tiltZero = readTilt();
}

static bool orbitUpdate(const Buttons &held, const Buttons &pressed){
  unsigned long now = gameMillis();

  // level up every 15 s
  int newLevel = 1 + now / levelDurationMs;
  if( newLevel != level ){
    level = newLevel;
    score += 25;
    playSfx(sfxLevel);
  }

  // steering
  float v = (readTilt() - tiltZero) * tiltGain;
  if( held.left ) v -= touchSpeed;
  if( held.right ) v += touchSpeed;
  shipX = constrain(shipX + v, (float)shipHalfW + 1, (float)(screenW - shipHalfW - 2));

  // shield
  shieldOn = held.shield && shield > 0;
  if( shieldOn ){
    shield = max(0.0f, shield - shieldDrainPerFrame);
  } else {
    shield = min(100.0f, shield + shieldRechargePerFrame);
  }

  // laser: hits the lowest debris in the column above the ship
  if( pressed.fire && now - lastLaser > laserCooldownMs ){
    lastLaser = now;
    laserX = (int)shipX;
    playSfx(sfxLaser);
    int target = -1;
    for( int i = 0; i < maxDebris; i++ ){
      Debris &d = debris[i];
      if( d.active && !d.isCell && fabsf(d.x - laserX) <= d.r + 1 && d.y < shipY ){
        if( target < 0 || d.y > debris[target].y ){
          target = i;
        }
      }
    }
    if( target >= 0 ){
      debris[target].active = false;
      debrisShot++;
      score += 5;
      playSfx(sfxBoom);
    }
  }

  // spawning gets faster with each level
  if( now >= nextSpawn ){
    spawn();
    unsigned long interval = max(220, 900 - level * 90);
    nextSpawn = now + interval + random(interval / 2);
  }

  // move debris and check collisions
  bool vulnerable = now >= invulnerableUntil;
  for( int i = 0; i < maxDebris; i++ ){
    Debris &d = debris[i];
    if( !d.active ){
      continue;
    }
    d.y += d.vy;
    d.x += d.vx;
    if( d.x < d.r || d.x > screenW - d.r ){
      d.vx = -d.vx;
    }

    bool hit = fabsf(d.x - shipX) < shipHalfW + d.r && fabsf(d.y - shipY) < shipHalfH + d.r;
    if( hit ){
      d.active = false;
      if( d.isCell ){
        cellsCollected++;
        score += 10;
        shield = min(100.0f, shield + 40);
        playSfx(sfxPickup);
      } else if( shieldOn ){
        score += 2;
        playSfx(sfxBoom);
      } else if( vulnerable ){
        lives--;
        invulnerableUntil = now + invulnerableMs;
        flashUntil = now + 120;
        playSfx(sfxHit);
        if( lives <= 0 ){
          return false;
        }
      }
      continue;
    }

    if( d.y - d.r > screenH ){
      d.active = false;
      if( !d.isCell ){
        score += 1;   // dodged
      }
    }
  }

  updateStars(1 + level * 0.3f, hudH);

  // LEDs: LED1 = danger (blinks while invulnerable), LED3 = shield
  bool blinking = !vulnerable && (now / 100) % 2;
  rgbLed(1, blinking || now < flashUntil);
  rgbLed(3, shieldOn);
  statusLed(lives == 1 && (now / 250) % 2);
  return true;
}

static void orbitDraw(){
  unsigned long now = gameMillis();
  display.clearDisplay();

  // HUD: score, shield bar, level, lives
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(score);
  display.drawRect(44, 1, 42, 6, SSD1306_WHITE);
  display.fillRect(45, 2, (int)(shield * 40 / 100), 4, SSD1306_WHITE);
  display.setCursor(90, 0);
  display.print("L");
  display.print(level);
  for( int i = 0; i < lives; i++ ){
    display.fillTriangle(120 - i * 7, 6, 126 - i * 7, 6, 123 - i * 7, 1, SSD1306_WHITE);
  }
  display.drawFastHLine(0, hudH - 1, screenW, SSD1306_WHITE);

  drawStars();

  for( int i = 0; i < maxDebris; i++ ){
    if( debris[i].active ){
      drawDebris(debris[i]);
    }
  }

  if( laserX >= 0 && now - lastLaser < laserVisibleMs ){
    display.drawFastVLine(laserX, hudH, shipY - 5 - hudH, SSD1306_WHITE);
    display.drawFastVLine(laserX + 1, hudH, shipY - 5 - hudH, SSD1306_WHITE);
  }

  bool visible = now >= invulnerableUntil || (now / 100) % 2;
  if( visible ){
    drawShip((int)shipX, shipY);
  }
  if( shieldOn ){
    display.drawCircle((int)shipX, shipY, 10, SSD1306_WHITE);
  }

  display.invertDisplay(now < flashUntil);
}

static uint32_t orbitScore(){
  return score;
}

static void orbitSummary(char *buf, size_t len){
  snprintf(buf, len, "L%d shot %d cells %d", level, debrisShot, cellsCollected);
}

extern const Game orbitGame = {
  "ORBIT DODGER", "orbit", "tilt to dodge debris",
  orbitStart, orbitUpdate, orbitDraw, orbitScore, orbitSummary, orbitResume,
};
