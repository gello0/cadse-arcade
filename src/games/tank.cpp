/* TANK BATTLE
 *
 * Drive your tank around the arena and shoot the enemy tanks before their
 * shells hit you. Enemies arrive faster over time and start to aim at you.
 *   Touch Up/Down/Left/Right (or tilt) : drive
 *   Left bumper                        : fire
 *
 * Ported to the CADSE arcade from "Tank.h" in
 * https://github.com/karthick965938/learn-tech-with-karthick (esp32_gaming_console)
 * with lives, a HUD, aiming enemies and tilt driving added.
 *
 * Original code: MIT License, Copyright (c) 2026 Karthick Nagaraj
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "../game.h"

const int tankHudH = 9;
const int tankSize = 8;
const int maxEnemyTanks = 3;
const float tankTiltDeadzone = 2.0f;
const unsigned long tankInvulnerableMs = 1500;

struct TankUnit {
  float x, y;
  int8_t dx, dy;
  bool active;
  unsigned long lastTurn;
};

struct Shell {
  float x, y;
  int8_t dx, dy;
  bool active;
};

static TankUnit player;
static TankUnit enemies[maxEnemyTanks];
static Shell playerShell;
static Shell enemyShells[maxEnemyTanks];
static uint32_t score;
static int lives;
static int kills;
static unsigned long lastSpawn, invulnerableUntil;
static float zeroX, zeroY;

static void tankStart(){
  player = {60, 50, 0, -1, true, 0};
  for( int i = 0; i < maxEnemyTanks; i++ ){
    enemies[i].active = false;
    enemyShells[i].active = false;
  }
  playerShell.active = false;
  score = 0;
  lives = 3;
  kills = 0;
  lastSpawn = 0;
  invulnerableUntil = 0;
  readTilt2(zeroX, zeroY);
}

static void tankResume(){
  readTilt2(zeroX, zeroY);
}

static void moveShell(Shell &s, float speed){
  if( !s.active ) return;
  s.x += s.dx * speed;
  s.y += s.dy * speed;
  if( s.x < 0 || s.x > screenW || s.y < tankHudH || s.y > screenH ){
    s.active = false;
  }
}

static bool shellHits(const Shell &s, const TankUnit &t){
  return s.active && t.active && s.x > t.x && s.x < t.x + tankSize && s.y > t.y && s.y < t.y + tankSize;
}

static bool tankUpdate(const Buttons &held, const Buttons &pressed){
  unsigned long now = gameMillis();

  // player: d-pad, or the stronger tilt axis
  float tx, ty;
  readTilt2(tx, ty);
  tx -= zeroX;
  ty -= zeroY;
  int mx = 0, my = 0;
  if( held.up ) my = -1;
  else if( held.down ) my = 1;
  else if( held.left ) mx = -1;
  else if( held.right ) mx = 1;
  else if( fabsf(tx) > tankTiltDeadzone || fabsf(ty) > tankTiltDeadzone ){
    if( fabsf(tx) > fabsf(ty) ) mx = tx > 0 ? 1 : -1;
    else my = ty > 0 ? 1 : -1;
  }
  if( mx || my ){
    player.x += mx * 1.5f;
    player.y += my * 1.5f;
    player.dx = mx;
    player.dy = my;
  }
  player.x = constrain(player.x, 0.0f, (float)(screenW - tankSize));
  player.y = constrain(player.y, (float)tankHudH, (float)(screenH - tankSize));

  if( pressed.fire && !playerShell.active ){
    playerShell = {player.x + 3, player.y + 3, player.dx, player.dy, true};
    playSfx(sfxBlip);
  }

  // enemies arrive faster as the score grows
  unsigned long spawnEvery = max(1000UL, 3000UL - kills * 100UL);
  if( now - lastSpawn > spawnEvery ){
    for( int i = 0; i < maxEnemyTanks; i++ ){
      if( !enemies[i].active ){
        enemies[i] = {(float)random(0, screenW - tankSize), (float)tankHudH, 0, 1, true, now};
        lastSpawn = now;
        break;
      }
    }
  }

  float enemySpeed = min(1.3f, 0.8f + kills * 0.03f);
  for( int i = 0; i < maxEnemyTanks; i++ ){
    TankUnit &e = enemies[i];
    if( !e.active ) continue;

    if( now - e.lastTurn > 1000 ){
      // half the time turn towards the player, otherwise wander
      if( random(2) ){
        float ddx = player.x - e.x, ddy = player.y - e.y;
        if( fabsf(ddx) > fabsf(ddy) ){ e.dx = ddx > 0 ? 1 : -1; e.dy = 0; }
        else { e.dx = 0; e.dy = ddy > 0 ? 1 : -1; }
      } else {
        static const int8_t dirs[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
        int d = random(4);
        e.dx = dirs[d][0];
        e.dy = dirs[d][1];
      }
      e.lastTurn = now;
      if( !enemyShells[i].active ){
        enemyShells[i] = {e.x + 3, e.y + 3, e.dx, e.dy, true};
      }
    }
    e.x = constrain(e.x + e.dx * enemySpeed, 0.0f, (float)(screenW - tankSize));
    e.y = constrain(e.y + e.dy * enemySpeed, (float)tankHudH, (float)(screenH - tankSize));
  }

  moveShell(playerShell, 3.0f);
  for( int i = 0; i < maxEnemyTanks; i++ ){
    moveShell(enemyShells[i], 2.0f);
  }

  for( int i = 0; i < maxEnemyTanks; i++ ){
    if( shellHits(playerShell, enemies[i]) ){
      enemies[i].active = false;
      playerShell.active = false;
      kills++;
      score += 50;
      playSfx(sfxBoom);
    }
  }

  if( now >= invulnerableUntil ){
    for( int i = 0; i < maxEnemyTanks; i++ ){
      if( shellHits(enemyShells[i], player) ){
        enemyShells[i].active = false;
        lives--;
        invulnerableUntil = now + tankInvulnerableMs;
        playSfx(sfxHit);
        if( lives <= 0 ){
          return false;
        }
      }
    }
  }

  rgbLed(1, now < invulnerableUntil && (now / 100) % 2);
  rgbLed(3, playerShell.active);
  return true;
}

static void drawTank(const TankUnit &t, bool filled){
  int x = (int)t.x, y = (int)t.y;
  if( filled ){
    display.fillRect(x, y, tankSize, tankSize, SSD1306_WHITE);
    display.drawPixel(x + 3, y + 3, SSD1306_BLACK);
  } else {
    display.drawRect(x, y, tankSize, tankSize, SSD1306_WHITE);
  }
  // barrel
  if( t.dy == -1 ) display.drawFastVLine(x + 3, y - 2, 3, SSD1306_WHITE);
  else if( t.dy == 1 ) display.drawFastVLine(x + 3, y + 7, 3, SSD1306_WHITE);
  else if( t.dx == -1 ) display.drawFastHLine(x - 2, y + 3, 3, SSD1306_WHITE);
  else display.drawFastHLine(x + 7, y + 3, 3, SSD1306_WHITE);
}

static void tankDraw(){
  unsigned long now = gameMillis();
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(score);
  for( int i = 0; i < lives; i++ ){
    display.drawRect(120 - i * 8, 1, 6, 6, SSD1306_WHITE);
  }
  display.drawFastHLine(0, tankHudH - 1, screenW, SSD1306_WHITE);

  if( now >= invulnerableUntil || (now / 100) % 2 ){
    drawTank(player, false);
  }
  for( int i = 0; i < maxEnemyTanks; i++ ){
    if( enemies[i].active ) drawTank(enemies[i], true);
  }
  if( playerShell.active ){
    display.fillCircle((int)playerShell.x, (int)playerShell.y, 1, SSD1306_WHITE);
  }
  for( int i = 0; i < maxEnemyTanks; i++ ){
    if( enemyShells[i].active ){
      display.drawPixel((int)enemyShells[i].x, (int)enemyShells[i].y, SSD1306_WHITE);
    }
  }
}

static uint32_t tankScore(){
  return score;
}

static void tankSummary(char *buf, size_t len){
  snprintf(buf, len, "tanks destroyed %d", kills);
}

extern const Game tankGame = {
  "TANK BATTLE", "tank", "drive, bumper fires",
  tankStart, tankUpdate, tankDraw, tankScore, tankSummary, tankResume,
};
