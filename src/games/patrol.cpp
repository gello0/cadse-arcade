/* DRONE PATROL
 *
 * Drive a rover through a random maze and collect every soil sample while
 * security drones hunt you. Power cores charge your EMP: fire it to freeze
 * nearby drones for a few seconds, then ram them for points.
 *   Touch pads (or tilt) : choose direction
 *   Left bumper          : fire EMP (needs a charge)
 * Clear all samples to move on to a new, faster maze.
 */
#include "../game.h"
#include "../maze.h"

const int ptlCell = 8;
const int ptlCols = 15;
const int ptlRows = 7;
const int ptlOriginX = 4;
const int ptlOriginY = 7;
const int maxDrones = 4;
const int numCores = 2;
const float empRadius = 28;
const unsigned long empFreezeMs = 4500;
const unsigned long droneRespawnMs = 3000;
const float ptlTiltDeadzone = 2.0f;

static const int8_t pdx[4] = {0, 1, 0, -1};   // N, E, S, W
static const int8_t pdy[4] = {-1, 0, 1, 0};

struct Mover {
  int cx, cy;      // cell we are leaving
  int dir;         // -1 = standing still
  float t;         // progress towards the next cell, 0..1
};

struct Drone {
  Mover m;
  bool active;
  unsigned long frozenUntil, respawnAt;
};

static Maze maze;
static bool samples[mazeMaxRows][mazeMaxCols];
static int samplesLeft;
static int coreX[numCores], coreY[numCores];
static bool coreTaken[numCores];
static Mover rover;
static int wantDir;
static Drone drones[maxDrones];
static int numDrones;
static int empCharges;
static unsigned long empFlashUntil;
static int level, lives;
static uint32_t score;
static int dronesRammed;
static unsigned long invulnerableUntil;
static float zeroX, zeroY;

static float moverX(const Mover &m){
  float x = ptlOriginX + m.cx * ptlCell + ptlCell / 2;
  return m.dir < 0 ? x : x + pdx[m.dir] * m.t * ptlCell;
}

static float moverY(const Mover &m){
  float y = ptlOriginY + m.cy * ptlCell + ptlCell / 2;
  return m.dir < 0 ? y : y + pdy[m.dir] * m.t * ptlCell;
}

// advance along the current direction; returns true when a cell is reached
static bool advance(Mover &m, float speed){
  if( m.dir < 0 ) return true;
  m.t += speed / ptlCell;
  if( m.t >= 1 ){
    m.cx += pdx[m.dir];
    m.cy += pdy[m.dir];
    m.t = 0;
    return true;
  }
  return false;
}

static void resetPositions(){
  rover = {0, 0, -1, 0};
  wantDir = -1;
  for( int i = 0; i < numDrones; i++ ){
    drones[i].m = {ptlCols - 1 - i % 2, ptlRows - 1 - i / 2, -1, 0};
    drones[i].active = true;
    drones[i].frozenUntil = 0;
  }
}

static void newLevel(){
  maze.generate(ptlCols, ptlRows);
  maze.braid(70);   // loops give you room to escape
  samplesLeft = 0;
  for( int y = 0; y < ptlRows; y++ ){
    for( int x = 0; x < ptlCols; x++ ){
      samples[y][x] = true;
      samplesLeft++;
    }
  }
  samples[0][0] = false;
  samplesLeft--;
  coreX[0] = ptlCols - 1; coreY[0] = 0;
  coreX[1] = 0; coreY[1] = ptlRows - 1;
  for( int i = 0; i < numCores; i++ ){
    coreTaken[i] = false;
  }
  numDrones = min(maxDrones, 1 + level);
  resetPositions();
  invulnerableUntil = gameMillis() + 1500;
}

static void patrolStart(){
  level = 1;
  lives = 3;
  score = 0;
  empCharges = 0;
  dronesRammed = 0;
  empFlashUntil = 0;
  readTilt2(zeroX, zeroY);
  newLevel();
}

static void patrolResume(){
  readTilt2(zeroX, zeroY);
}

static int chooseDroneDir(const Drone &d){
  const Mover &m = d.m;
  int options[4], n = 0;
  int reverse = m.dir >= 0 ? (m.dir + 2) % 4 : -1;
  for( int dir = 0; dir < 4; dir++ ){
    if( maze.open(m.cx, m.cy, dir) && dir != reverse ) options[n++] = dir;
  }
  if( n == 0 ) return reverse;   // dead end: turn around

  // chase more often on later levels
  if( random(100) < 45 + level * 8 ){
    int best = options[0];
    int bestDist = 9999;
    for( int i = 0; i < n; i++ ){
      int nx = m.cx + pdx[options[i]], ny = m.cy + pdy[options[i]];
      int dist = abs(nx - rover.cx) + abs(ny - rover.cy);
      if( dist < bestDist ){ bestDist = dist; best = options[i]; }
    }
    return best;
  }
  return options[random(n)];
}

static bool patrolUpdate(const Buttons &held, const Buttons &pressed){
  unsigned long now = gameMillis();

  // input: touch pads, else the stronger tilt axis
  float tx, ty;
  readTilt2(tx, ty);
  tx -= zeroX;
  ty -= zeroY;
  if( held.up ) wantDir = 0;
  else if( held.right ) wantDir = 1;
  else if( held.down ) wantDir = 2;
  else if( held.left ) wantDir = 3;
  else if( fabsf(tx) > ptlTiltDeadzone || fabsf(ty) > ptlTiltDeadzone ){
    if( fabsf(tx) > fabsf(ty) ) wantDir = tx > 0 ? 1 : 3;
    else wantDir = ty > 0 ? 2 : 0;
  }

  // reversing mid-corridor is always allowed
  if( rover.dir >= 0 && wantDir == (rover.dir + 2) % 4 ){
    rover.cx += pdx[rover.dir];
    rover.cy += pdy[rover.dir];
    rover.dir = wantDir;
    rover.t = 1 - rover.t;
  }

  float roverSpeed = 1.1f;
  if( advance(rover, roverSpeed) ){
    if( samples[rover.cy][rover.cx] ){
      samples[rover.cy][rover.cx] = false;
      samplesLeft--;
      score += 5;
      playSfx(sfxClick);
    }
    for( int i = 0; i < numCores; i++ ){
      if( !coreTaken[i] && rover.cx == coreX[i] && rover.cy == coreY[i] ){
        coreTaken[i] = true;
        empCharges++;
        score += 20;
        playSfx(sfxPickup);
      }
    }
    if( wantDir >= 0 && maze.open(rover.cx, rover.cy, wantDir) ){
      rover.dir = wantDir;
    } else if( rover.dir >= 0 && !maze.open(rover.cx, rover.cy, rover.dir) ){
      rover.dir = -1;
    }
  }

  if( samplesLeft == 0 ){
    level++;
    score += 200;
    playSfx(sfxLevel);
    newLevel();
    return true;
  }

  // EMP freezes drones close to the rover
  if( pressed.fire && empCharges > 0 ){
    empCharges--;
    empFlashUntil = now + 150;
    playSfx(sfxBoom);
    float rx = moverX(rover), ry = moverY(rover);
    for( int i = 0; i < numDrones; i++ ){
      float dx = moverX(drones[i].m) - rx, dy = moverY(drones[i].m) - ry;
      if( drones[i].active && dx * dx + dy * dy < empRadius * empRadius ){
        drones[i].frozenUntil = now + empFreezeMs;
      }
    }
  }

  float droneSpeed = min(1.05f, 0.6f + level * 0.08f);
  float rx = moverX(rover), ry = moverY(rover);
  for( int i = 0; i < numDrones; i++ ){
    Drone &d = drones[i];
    if( !d.active ){
      if( now >= d.respawnAt ){
        d.active = true;
        d.m = {ptlCols - 1, ptlRows - 1, -1, 0};
      }
      continue;
    }
    bool frozen = now < d.frozenUntil;
    if( !frozen && advance(d.m, droneSpeed) ){
      d.m.dir = chooseDroneDir(d);
    }

    float dx = moverX(d.m) - rx, dy = moverY(d.m) - ry;
    if( dx * dx + dy * dy < 25 ){
      if( frozen ){
        d.active = false;
        d.respawnAt = now + droneRespawnMs;
        dronesRammed++;
        score += 100;
        playSfx(sfxBoom);
      } else if( now >= invulnerableUntil ){
        lives--;
        playSfx(sfxHit);
        if( lives <= 0 ){
          return false;
        }
        resetPositions();
        invulnerableUntil = now + 1500;
        break;
      }
    }
  }

  rgbLed(1, now < invulnerableUntil && (now / 100) % 2);
  rgbLed(3, empCharges > 0);
  return true;
}

static void patrolDraw(){
  unsigned long now = gameMillis();
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(score);
  display.setCursor(48, 0);
  display.printf("M%d EMP%d", level, empCharges);
  for( int i = 0; i < lives; i++ ){
    display.fillRect(122 - i * 6, 1, 4, 4, SSD1306_WHITE);
  }

  for( int y = 0; y < ptlRows; y++ ){
    for( int x = 0; x < ptlCols; x++ ){
      int px = ptlOriginX + x * ptlCell;
      int py = ptlOriginY + y * ptlCell;
      if( maze.walls[y][x] & WALL_N ) display.drawFastHLine(px, py, ptlCell + 1, SSD1306_WHITE);
      if( maze.walls[y][x] & WALL_W ) display.drawFastVLine(px, py, ptlCell + 1, SSD1306_WHITE);
      if( samples[y][x] ) display.drawPixel(px + 4, py + 4, SSD1306_WHITE);
    }
  }
  display.drawFastHLine(ptlOriginX, ptlOriginY + ptlRows * ptlCell, ptlCols * ptlCell + 1, SSD1306_WHITE);
  display.drawFastVLine(ptlOriginX + ptlCols * ptlCell, ptlOriginY, ptlRows * ptlCell + 1, SSD1306_WHITE);

  for( int i = 0; i < numCores; i++ ){
    if( !coreTaken[i] && (millis() / 200) % 2 ){
      display.drawRect(ptlOriginX + coreX[i] * ptlCell + 2, ptlOriginY + coreY[i] * ptlCell + 2, 5, 5, SSD1306_WHITE);
    }
  }

  for( int i = 0; i < numDrones; i++ ){
    if( !drones[i].active ) continue;
    int x = lroundf(moverX(drones[i].m)), y = lroundf(moverY(drones[i].m));
    bool frozen = now < drones[i].frozenUntil;
    if( frozen && (millis() / 120) % 2 ){
      display.drawPixel(x, y, SSD1306_WHITE);
      continue;
    }
    display.drawLine(x, y - 3, x + 3, y, SSD1306_WHITE);
    display.drawLine(x + 3, y, x, y + 3, SSD1306_WHITE);
    display.drawLine(x, y + 3, x - 3, y, SSD1306_WHITE);
    display.drawLine(x - 3, y, x, y - 3, SSD1306_WHITE);
  }

  if( now >= invulnerableUntil || (now / 100) % 2 ){
    int x = lroundf(moverX(rover)), y = lroundf(moverY(rover));
    display.fillRect(x - 2, y - 2, 5, 5, SSD1306_WHITE);
    display.drawPixel(x, y, SSD1306_BLACK);
  }

  if( now < empFlashUntil ){
    display.drawCircle(lroundf(moverX(rover)), lroundf(moverY(rover)), (int)empRadius, SSD1306_WHITE);
  }
}

static uint32_t patrolScore(){
  return score;
}

static void patrolSummary(char *buf, size_t len){
  snprintf(buf, len, "maze %d rammed %d", level, dronesRammed);
}

extern const Game patrolGame = {
  "DRONE PATROL", "patrol", "samples & EMP drones",
  patrolStart, patrolUpdate, patrolDraw, patrolScore, patrolSummary, patrolResume,
};
