/* COMET TAIL
 *
 * Steer a growing comet around the arena and swallow stardust.
 *   Touch Up/Down/Left/Right : change direction
 *   Left / right bumper      : turn left / right relative to the comet
 * Don't hit the walls or your own tail. Every 5th stardust spawns a nova
 * worth 50 points that fades after a few seconds. The comet speeds up as
 * it grows.
 */
#include "../game.h"

const int cometCell = 4;
const int cometCols = 31;
const int cometRows = 13;
const int cometOriginX = 2;
const int cometOriginY = 11;
const int cometMaxLen = cometCols * cometRows;
const unsigned long novaLifetimeMs = 6000;

static const int8_t dirX[4] = {0, 1, 0, -1};   // up, right, down, left
static const int8_t dirY[4] = {-1, 0, 1, 0};

static uint8_t segX[cometMaxLen], segY[cometMaxLen];
static int length;
static int dir, nextDir;
static int dustX, dustY;
static bool novaActive;
static int novaX, novaY;
static unsigned long novaUntil;
static unsigned long stepMs, lastStep;
static uint32_t score;
static int eaten, novasEaten;

static bool occupied(int x, int y){
  for( int i = 0; i < length; i++ ){
    if( segX[i] == x && segY[i] == y ){
      return true;
    }
  }
  return false;
}

// Pick into locals first: callers pass dustX/dustY or novaX/novaY themselves,
// and comparing against them while writing them would never terminate.
static void randomFreeCell(int &outX, int &outY){
  int x, y;
  do {
    x = random(cometCols);
    y = random(cometRows);
  } while( occupied(x, y) || (novaActive && x == novaX && y == novaY) || (x == dustX && y == dustY) );
  outX = x;
  outY = y;
}

static void cometStart(){
  length = 4;
  for( int i = 0; i < length; i++ ){
    segX[i] = 8 - i;
    segY[i] = cometRows / 2;
  }
  dir = nextDir = 1;
  novaActive = false;
  dustX = dustY = -1;
  randomFreeCell(dustX, dustY);
  stepMs = 160;
  lastStep = 0;
  score = 0;
  eaten = 0;
  novasEaten = 0;
}

static void turnTo(int d){
  if( (d + 2) % 4 != dir ){   // no reversing into yourself
    nextDir = d;
  }
}

static bool cometUpdate(const Buttons &held, const Buttons &pressed){
  unsigned long now = gameMillis();

  if( pressed.up ) turnTo(0);
  if( pressed.right ) turnTo(1);
  if( pressed.down ) turnTo(2);
  if( pressed.left ) turnTo(3);
  if( pressed.fire ) nextDir = (dir + 3) % 4;
  if( pressed.shield ) nextDir = (dir + 1) % 4;

  if( novaActive && now > novaUntil ){
    novaActive = false;
  }

  updateStars(0.15f, cometOriginY);

  if( now - lastStep < stepMs ){
    return true;
  }
  lastStep = now;
  dir = nextDir;

  int nx = segX[0] + dirX[dir];
  int ny = segY[0] + dirY[dir];
  if( nx < 0 || ny < 0 || nx >= cometCols || ny >= cometRows ){
    playSfx(sfxHit);
    return false;
  }

  bool ateDust = nx == dustX && ny == dustY;
  bool ateNova = novaActive && nx == novaX && ny == novaY;
  bool grow = ateDust || ateNova;

  // the last segment moves away this step unless we grow
  int check = grow ? length : length - 1;
  for( int i = 0; i < check; i++ ){
    if( segX[i] == nx && segY[i] == ny ){
      playSfx(sfxHit);
      return false;
    }
  }

  if( grow && length < cometMaxLen ){
    length++;
  }
  for( int i = length - 1; i > 0; i-- ){
    segX[i] = segX[i - 1];
    segY[i] = segY[i - 1];
  }
  segX[0] = nx;
  segY[0] = ny;

  if( ateDust ){
    eaten++;
    score += 10 + (160 - stepMs) / 10;
    stepMs = max(60UL, stepMs - 4);
    randomFreeCell(dustX, dustY);
    if( eaten % 5 == 0 && !novaActive ){
      randomFreeCell(novaX, novaY);
      novaActive = true;
      novaUntil = now + novaLifetimeMs;
      playSfx(sfxLevel);
    } else {
      playSfx(sfxPickup);
    }
    rgbLed(3, true);
  } else {
    rgbLed(3, false);
  }
  if( ateNova ){
    novasEaten++;
    novaActive = false;
    score += 50;
    playSfx(sfxBoom);
  }
  return true;
}

static void cometDraw(){
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(score);
  display.setCursor(90, 0);
  display.printf("len %d", length);

  display.drawRect(cometOriginX - 1, cometOriginY - 1,
                   cometCols * cometCell + 1, cometRows * cometCell + 1, SSD1306_WHITE);
  drawStars();

  for( int i = length - 1; i >= 0; i-- ){
    int x = cometOriginX + segX[i] * cometCell;
    int y = cometOriginY + segY[i] * cometCell;
    if( i == 0 ){
      display.fillRect(x - 1, y - 1, 5, 5, SSD1306_WHITE);      // bright head
    } else if( i > length - 3 ){
      display.drawPixel(x + 1, y + 1, SSD1306_WHITE);           // fading tail tip
    } else if( i % 2 ){
      display.drawRect(x, y, 3, 3, SSD1306_WHITE);
    } else {
      display.fillRect(x, y, 3, 3, SSD1306_WHITE);
    }
  }

  // stardust: a small twinkling plus
  int dx = cometOriginX + dustX * cometCell + 1;
  int dy = cometOriginY + dustY * cometCell + 1;
  display.drawPixel(dx, dy, SSD1306_WHITE);
  if( (millis() / 200) % 2 ){
    display.drawPixel(dx - 1, dy, SSD1306_WHITE);
    display.drawPixel(dx + 1, dy, SSD1306_WHITE);
    display.drawPixel(dx, dy - 1, SSD1306_WHITE);
    display.drawPixel(dx, dy + 1, SSD1306_WHITE);
  }

  // nova blinks faster as it is about to fade
  if( novaActive ){
    unsigned long left = novaUntil - gameMillis();
    unsigned long period = left < 2000 ? 80 : 250;
    if( (millis() / period) % 2 ){
      int nx = cometOriginX + novaX * cometCell + 1;
      int ny = cometOriginY + novaY * cometCell + 1;
      display.fillTriangle(nx - 3, ny, nx + 3, ny, nx, ny - 3, SSD1306_WHITE);
      display.fillTriangle(nx - 3, ny, nx + 3, ny, nx, ny + 3, SSD1306_WHITE);
    }
  }
}

static uint32_t cometScore(){
  return score;
}

static void cometSummary(char *buf, size_t len){
  snprintf(buf, len, "length %d novas %d", length, novasEaten);
}

extern const Game cometGame = {
  "COMET TAIL", "comet", "eat dust, dodge tail",
  cometStart, cometUpdate, cometDraw, cometScore, cometSummary, nullptr,
};
