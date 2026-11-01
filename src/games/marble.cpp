/* GRAVITY MARBLE
 *
 * Tilt the board to roll a marble through a random maze to the exit before
 * the clock runs out. From maze 3 on, black holes sit in dead ends and
 * swallow the marble, sending it back to the start and costing 5 s.
 *   Tilt in any direction : roll
 *   Touch pads            : nudge the marble (no IMU needed)
 */
#include "../game.h"
#include "../maze.h"

const int mblCell = 8;
const int mblCols = 15;
const int mblRows = 7;
const int mblOriginX = 4;
const int mblOriginY = 7;
const int maxHoles = 4;

const float mblAccel = 0.05f;      // px/frame^2 per m/s^2
const float mblNudge = 0.25f;
const float mblFriction = 0.95f;
const float mblMaxSpeed = 2.4f;
const unsigned long holePenaltyMs = 5000;

static Maze maze;
static float mx, my, mvx, mvy;
static int holeX[maxHoles], holeY[maxHoles];
static int numHoles;
static int level;
static uint32_t score;
static unsigned long levelStart, timeLimit;
static float zeroX, zeroY;
static int holesFallen;

static float cellCenterX(int cx){ return mblOriginX + cx * mblCell + mblCell / 2; }
static float cellCenterY(int cy){ return mblOriginY + cy * mblCell + mblCell / 2; }

// Is this screen pixel part of a wall? Walls are 1 px lines on the cell grid.
static bool wallPixel(int px, int py){
  int lx = px - mblOriginX, ly = py - mblOriginY;
  if( lx < 0 || ly < 0 || lx > mblCols * mblCell || ly > mblRows * mblCell ){
    return true;
  }
  bool onV = lx % mblCell == 0;
  bool onH = ly % mblCell == 0;
  if( onV && onH ){
    return true;   // corner posts are always solid
  }
  int cx = lx / mblCell, cy = ly / mblCell;
  if( onV ){
    if( cx == 0 || cx == mblCols ) return true;
    return maze.walls[cy][cx] & WALL_W;
  }
  if( onH ){
    if( cy == 0 || cy == mblRows ) return true;
    return maze.walls[cy][cx] & WALL_N;
  }
  return false;
}

static bool marbleBlocked(float x, float y){
  int cx = lroundf(x), cy = lroundf(y);
  for( int dy = -1; dy <= 1; dy++ ){
    for( int dx = -1; dx <= 1; dx++ ){
      if( wallPixel(cx + dx, cy + dy) ) return true;
    }
  }
  return false;
}

static void placeMarbleAtStart(){
  mx = cellCenterX(0);
  my = cellCenterY(0);
  mvx = mvy = 0;
}

static void newMaze(){
  maze.generate(mblCols, mblRows);
  numHoles = 0;
  if( level >= 3 ){
    int wanted = min(level - 2, maxHoles);
    for( int tries = 0; tries < 200 && numHoles < wanted; tries++ ){
      int x = random(mblCols), y = random(mblRows);
      bool nearStart = x < 3 && y < 3;
      bool isExit = x == mblCols - 1 && y == mblRows - 1;
      if( !nearStart && !isExit && maze.countWalls(x, y) == 3 ){
        bool dup = false;
        for( int i = 0; i < numHoles; i++ ){
          if( holeX[i] == x && holeY[i] == y ) dup = true;
        }
        if( !dup ){
          holeX[numHoles] = x;
          holeY[numHoles] = y;
          numHoles++;
        }
      }
    }
  }
  levelStart = gameMillis();
  timeLimit = max(20000UL, 45000UL - (unsigned long)(level - 1) * 3000UL);
  placeMarbleAtStart();
}

static void marbleStart(){
  level = 1;
  score = 0;
  holesFallen = 0;
  readTilt2(zeroX, zeroY);
  newMaze();
}

static void marbleResume(){
  readTilt2(zeroX, zeroY);
}

static long timeLeft(){
  return (long)timeLimit - (long)(gameMillis() - levelStart);
}

static bool marbleUpdate(const Buttons &held, const Buttons &pressed){
  float tx, ty;
  readTilt2(tx, ty);
  mvx += (tx - zeroX) * mblAccel;
  mvy += (ty - zeroY) * mblAccel;
  if( held.left ) mvx -= mblNudge;
  if( held.right ) mvx += mblNudge;
  if( held.up ) mvy -= mblNudge;
  if( held.down ) mvy += mblNudge;
  mvx = constrain(mvx * mblFriction, -mblMaxSpeed, mblMaxSpeed);
  mvy = constrain(mvy * mblFriction, -mblMaxSpeed, mblMaxSpeed);

  // move each axis separately in small steps and bounce off walls
  const int steps = 3;
  bool bumped = false;
  for( int s = 0; s < steps; s++ ){
    float nx = mx + mvx / steps;
    if( marbleBlocked(nx, my) ){
      bumped |= fabsf(mvx) > 0.8f;
      mvx = -mvx * 0.35f;
    } else {
      mx = nx;
    }
    float ny = my + mvy / steps;
    if( marbleBlocked(mx, ny) ){
      bumped |= fabsf(mvy) > 0.8f;
      mvy = -mvy * 0.35f;
    } else {
      my = ny;
    }
  }
  if( bumped ){
    playSfx(sfxClick);
  }

  // black holes
  for( int i = 0; i < numHoles; i++ ){
    float dx = mx - cellCenterX(holeX[i]), dy = my - cellCenterY(holeY[i]);
    if( dx * dx + dy * dy < 6 ){
      holesFallen++;
      levelStart -= holePenaltyMs;
      playSfx(sfxHit);
      rgbLed(1, true);
      placeMarbleAtStart();
      break;
    }
  }

  // exit
  float ex = mx - cellCenterX(mblCols - 1), ey = my - cellCenterY(mblRows - 1);
  if( ex * ex + ey * ey < 6 ){
    score += 100 * level + max(0L, timeLeft() / 1000) * 5;
    level++;
    playSfx(sfxLevel);
    newMaze();
  }

  long left = timeLeft();
  if( left <= 0 ){
    return false;
  }
  rgbLed(1, left < 5000 && (millis() / 150) % 2);
  rgbLed(3, left >= 5000);
  return true;
}

static void marbleDraw(){
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(score);
  display.setCursor(56, 0);
  display.printf("M%d", level);
  long left = max(0L, timeLeft());
  display.setCursor(100, 0);
  display.printf("%2lds", (left + 999) / 1000);

  for( int y = 0; y < mblRows; y++ ){
    for( int x = 0; x < mblCols; x++ ){
      int px = mblOriginX + x * mblCell;
      int py = mblOriginY + y * mblCell;
      if( maze.walls[y][x] & WALL_N ) display.drawFastHLine(px, py, mblCell + 1, SSD1306_WHITE);
      if( maze.walls[y][x] & WALL_W ) display.drawFastVLine(px, py, mblCell + 1, SSD1306_WHITE);
    }
  }
  display.drawFastHLine(mblOriginX, mblOriginY + mblRows * mblCell, mblCols * mblCell + 1, SSD1306_WHITE);
  display.drawFastVLine(mblOriginX + mblCols * mblCell, mblOriginY, mblRows * mblCell + 1, SSD1306_WHITE);

  // exit: pulsing ring
  int r = 1 + (millis() / 250) % 3;
  display.drawCircle(cellCenterX(mblCols - 1), cellCenterY(mblRows - 1), r, SSD1306_WHITE);

  for( int i = 0; i < numHoles; i++ ){
    int hx = cellCenterX(holeX[i]), hy = cellCenterY(holeY[i]);
    display.drawCircle(hx, hy, 2, SSD1306_WHITE);
    display.drawPixel(hx, hy, SSD1306_WHITE);
  }

  display.fillCircle(lroundf(mx), lroundf(my), 1, SSD1306_WHITE);
}

static uint32_t marbleScore(){
  return score;
}

static void marbleSummary(char *buf, size_t len){
  snprintf(buf, len, "maze %d holes %d", level, holesFallen);
}

extern const Game marbleGame = {
  "GRAVITY MARBLE", "marble", "tilt to the exit",
  marbleStart, marbleUpdate, marbleDraw, marbleScore, marbleSummary, marbleResume,
};
