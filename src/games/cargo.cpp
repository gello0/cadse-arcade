/* CARGO DROP
 *
 * Stack cargo pods in the hold. Pods are short three-cell bars, corners and
 * two-cell crates; a full row is shipped off and scores. Now and then a
 * bomb pod drops: it blasts a 3x3 hole wherever it lands.
 *   Touch Left/Right (or tilt) : shift the pod
 *   Touch Up / right bumper    : rotate
 *   Touch Down                 : drop faster
 *   Left bumper                : slam down
 * The hold fills faster every 8 rows shipped.
 */
#include "../game.h"

const int holdCols = 10;
const int holdRows = 18;
const int podCell = 3;
const int holdX = 49;
const int holdY = 9;
const float cargoTiltThreshold = 2.5f;
const unsigned long cargoRepeatMs = 140;

enum PodType { POD_BAR, POD_CORNER, POD_CRATE, POD_BOMB, NUM_POD_TYPES };

struct Pod {
  PodType type;
  int8_t cells[3][2];   // offsets from the pivot
  int count;
  int x, y;
};

static uint8_t hold[holdRows][holdCols];   // 0 empty, 1 cargo
static Pod pod;
static PodType nextType;
static uint32_t score;
static int rowsShipped;
static int level;
static unsigned long lastFall, lastShift, flashUntil;
static float zeroX;

static PodType randomType(){
  int r = random(100);
  if( r < 8 ) return POD_BOMB;
  if( r < 40 ) return POD_BAR;
  if( r < 75 ) return POD_CORNER;
  return POD_CRATE;
}

static void makePod(Pod &p, PodType type){
  p.type = type;
  switch( type ){
    case POD_BAR:
      p.count = 3;
      p.cells[0][0] = -1; p.cells[0][1] = 0;
      p.cells[1][0] = 0;  p.cells[1][1] = 0;
      p.cells[2][0] = 1;  p.cells[2][1] = 0;
      break;
    case POD_CORNER:
      p.count = 3;
      p.cells[0][0] = 0; p.cells[0][1] = 0;
      p.cells[1][0] = 1; p.cells[1][1] = 0;
      p.cells[2][0] = 0; p.cells[2][1] = 1;
      break;
    case POD_CRATE:
      p.count = 2;
      p.cells[0][0] = 0; p.cells[0][1] = 0;
      p.cells[1][0] = 1; p.cells[1][1] = 0;
      break;
    default:
      p.count = 1;
      p.cells[0][0] = 0; p.cells[0][1] = 0;
      break;
  }
  p.x = holdCols / 2 - 1;
  p.y = 0;
}

static bool fits(const Pod &p){
  for( int i = 0; i < p.count; i++ ){
    int x = p.x + p.cells[i][0];
    int y = p.y + p.cells[i][1];
    if( x < 0 || x >= holdCols || y >= holdRows ) return false;
    if( y >= 0 && hold[y][x] ) return false;
  }
  return true;
}

static void rotatePod(){
  if( pod.type == POD_BOMB ) return;
  Pod turned = pod;
  for( int i = 0; i < pod.count; i++ ){
    turned.cells[i][0] = -pod.cells[i][1];
    turned.cells[i][1] = pod.cells[i][0];
  }
  // try in place, then nudged sideways off a wall
  const int kicks[3] = {0, -1, 1};
  for( int k : kicks ){
    turned.x = pod.x + k;
    if( fits(turned) ){
      pod = turned;
      playSfx(sfxClick);
      return;
    }
  }
}

static bool spawnPod(){
  makePod(pod, nextType);
  nextType = randomType();
  return fits(pod);
}

static void shipFullRows(){
  int shipped = 0;
  for( int y = holdRows - 1; y >= 0; y-- ){
    bool full = true;
    for( int x = 0; x < holdCols; x++ ){
      if( !hold[y][x] ){ full = false; break; }
    }
    if( !full ) continue;
    shipped++;
    for( int yy = y; yy > 0; yy-- ){
      memcpy(hold[yy], hold[yy - 1], holdCols);
    }
    memset(hold[0], 0, holdCols);
    y++;   // check the same row again after shifting
  }
  if( shipped ){
    static const int rowPoints[4] = {0, 10, 30, 60};
    score += rowPoints[min(shipped, 3)] * level;
    rowsShipped += shipped;
    level = 1 + rowsShipped / 8;
    flashUntil = gameMillis() + 120;
    playSfx(shipped > 1 ? sfxLevel : sfxPickup);
  }
}

// returns false when the next pod has no room
static bool landPod(){
  if( pod.type == POD_BOMB ){
    for( int dy = -1; dy <= 1; dy++ ){
      for( int dx = -1; dx <= 1; dx++ ){
        int x = pod.x + dx, y = pod.y + dy;
        if( x >= 0 && x < holdCols && y >= 0 && y < holdRows ){
          hold[y][x] = 0;
        }
      }
    }
    score += 5;
    flashUntil = gameMillis() + 120;
    playSfx(sfxBoom);
  } else {
    for( int i = 0; i < pod.count; i++ ){
      int y = pod.y + pod.cells[i][1];
      if( y < 0 ) return false;   // stacked out of the hold
      hold[y][pod.x + pod.cells[i][0]] = 1;
    }
    playSfx(sfxBlip);
    shipFullRows();
  }
  return spawnPod();
}

static void cargoStart(){
  memset(hold, 0, sizeof(hold));
  score = 0;
  rowsShipped = 0;
  level = 1;
  lastFall = lastShift = 0;
  flashUntil = 0;
  zeroX = readTilt();
  nextType = randomType();
  spawnPod();
}

static void cargoResume(){
  zeroX = readTilt();
}

static bool tryMove(int dx, int dy){
  Pod moved = pod;
  moved.x += dx;
  moved.y += dy;
  if( fits(moved) ){
    pod = moved;
    return true;
  }
  return false;
}

static bool cargoUpdate(const Buttons &held, const Buttons &pressed){
  unsigned long now = gameMillis();

  if( pressed.up || pressed.shield ) rotatePod();

  // sideways: tap, hold to repeat, or tilt
  float tilt = readTilt() - zeroX;
  int shift = 0;
  if( held.left || tilt < -cargoTiltThreshold ) shift = -1;
  if( held.right || tilt > cargoTiltThreshold ) shift = 1;
  if( pressed.left || pressed.right || (shift && now - lastShift > cargoRepeatMs) ){
    if( shift && tryMove(shift, 0) ){
      lastShift = now;
    }
  }

  if( pressed.fire ){
    int dropped = 0;
    while( tryMove(0, 1) ) dropped++;
    score += dropped;
    return landPod();
  }

  unsigned long fallMs = max(80UL, 600UL - (unsigned long)(level - 1) * 55UL);
  if( held.down ) fallMs = min(fallMs, 50UL);
  if( now - lastFall >= fallMs ){
    lastFall = now;
    if( !tryMove(0, 1) ){
      return landPod();
    }
  }

  rgbLed(3, nextType == POD_BOMB);
  return true;
}

static void drawCell(int x, int y, bool bomb){
  int px = holdX + x * podCell, py = holdY + y * podCell;
  if( bomb ){
    display.drawPixel(px + 1, py, SSD1306_WHITE);
    display.drawFastHLine(px, py + 1, 3, SSD1306_WHITE);
    display.drawPixel(px + 1, py + 2, SSD1306_WHITE);
  } else {
    display.fillRect(px, py, podCell - 1, podCell - 1, SSD1306_WHITE);
  }
}

static void cargoDraw(){
  display.clearDisplay();
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("SCORE");
  display.setCursor(0, 10);
  display.print(score);
  display.setCursor(0, 26);
  display.print("ROWS");
  display.setCursor(0, 36);
  display.print(rowsShipped);
  display.setCursor(0, 52);
  display.printf("LV %d", level);

  // hold outline
  display.drawFastVLine(holdX - 2, holdY - 1, holdRows * podCell + 2, SSD1306_WHITE);
  display.drawFastVLine(holdX + holdCols * podCell, holdY - 1, holdRows * podCell + 2, SSD1306_WHITE);
  display.drawFastHLine(holdX - 2, holdY + holdRows * podCell, holdCols * podCell + 3, SSD1306_WHITE);

  for( int y = 0; y < holdRows; y++ ){
    for( int x = 0; x < holdCols; x++ ){
      if( hold[y][x] ) drawCell(x, y, false);
    }
  }

  // ghost: where the pod would land
  Pod ghost = pod;
  while( true ){
    ghost.y++;
    if( !fits(ghost) ){ ghost.y--; break; }
  }
  for( int i = 0; i < ghost.count; i++ ){
    int y = ghost.y + ghost.cells[i][1];
    if( y >= 0 ){
      display.drawPixel(holdX + (ghost.x + ghost.cells[i][0]) * podCell + 1, holdY + y * podCell + 1, SSD1306_WHITE);
    }
  }

  for( int i = 0; i < pod.count; i++ ){
    int y = pod.y + pod.cells[i][1];
    if( y >= 0 ) drawCell(pod.x + pod.cells[i][0], y, pod.type == POD_BOMB);
  }

  // next pod preview
  display.setCursor(92, 0);
  display.print("NEXT");
  Pod preview;
  makePod(preview, nextType);
  for( int i = 0; i < preview.count; i++ ){
    int px = 100 + preview.cells[i][0] * 4;
    int py = 14 + preview.cells[i][1] * 4;
    if( preview.type == POD_BOMB ){
      display.drawCircle(px + 1, py + 1, 2, SSD1306_WHITE);
    } else {
      display.fillRect(px, py, 3, 3, SSD1306_WHITE);
    }
  }

  display.invertDisplay(gameMillis() < flashUntil);
}

static uint32_t cargoScore(){
  return score;
}

static void cargoSummary(char *buf, size_t len){
  snprintf(buf, len, "rows %d level %d", rowsShipped, level);
}

extern const Game cargoGame = {
  "CARGO DROP", "cargo", "stack pods, ship rows",
  cargoStart, cargoUpdate, cargoDraw, cargoScore, cargoSummary, cargoResume,
};
