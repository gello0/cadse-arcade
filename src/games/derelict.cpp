/* DERELICT - first-person raycaster on an abandoned space station
 *
 * Find the exit of each deck while security droids hunt you.
 *   Tilt left / right    turn (touch Left/Right turn too if there is no IMU)
 *   Touch Up / Down      walk forward / back
 *   Touch Left / Right   strafe
 *   Left bumper          fire
 *   Right bumper         open doors, use the exit
 * Deck 1 is hand-built and has a keycard door; after that every deck is a
 * random maze with more droids. Ammo, medkits and keycards lie around.
 *
 * The renderer casts one ray per screen column (DDA), shades walls with an
 * ordered dither by distance, and draws scaled 16x16 sprites behind a
 * per-column depth buffer. Pixels go straight into the SSD1306 buffer.
 */
#include "../game.h"
#include "../maze.h"

const int viewW = 128;
const int viewH = 56;                     // HUD below
const int mapSize = 16;
const int maxThings = 28;
const float fovPlane = 0.66f;             // ~66 degree field of view
const float playerRadius = 0.22f;
const float moveSpeed = 0.07f;            // cells per frame
const float turnSpeed = 0.07f;            // radians per frame from touch
const float tiltTurnGain = 0.022f;        // radians per frame per m/s^2
const float tiltDeadzone = 0.8f;
const unsigned long fireCooldownMs = 280;
const unsigned long messageMs = 1600;

enum Tile : uint8_t { T_EMPTY = 0, T_PANEL = 1, T_BRICK = 2, T_TECH = 3, T_DOOR = 4, T_LOCKED = 5, T_EXIT = 6 };
enum ThingType : uint8_t { TH_DROID, TH_AMMO, TH_HEALTH, TH_KEY };
enum DroidState : uint8_t { DS_IDLE, DS_CHASE, DS_DYING };

struct Thing {
  bool alive;
  ThingType type;
  float x, y;
  int8_t hp;
  DroidState state;
  unsigned long nextShot, flashUntil, dyingSince;
};

// ---------------------------------------------------------------- sprites (16x16, MSB = left)

static const uint16_t droidSprite[16] = {
  0b0000011111100000,
  0b0001111111111000,
  0b0011111111111100,
  0b0111100000011110,
  0b0111011111101110,
  0b1110110000110111,
  0b1110110110110111,
  0b1110110000110111,
  0b0111011111101110,
  0b0111100000011110,
  0b0011111111111100,
  0b0001111111111000,
  0b0000110000110000,
  0b0001100000011000,
  0b0011000000001100,
  0b0000000000000000,
};

static const uint16_t droidFiringSprite[16] = {
  0b0000011111100000,
  0b0001111111111000,
  0b0011111111111100,
  0b0111100000011110,
  0b0111011111101110,
  0b1110111111110111,
  0b1110111111110111,
  0b1110111111110111,
  0b0111011111101110,
  0b0111100000011110,
  0b0011111111111100,
  0b0001111111111000,
  0b0000110000110000,
  0b0001100000011000,
  0b0011000000001100,
  0b0000000000000000,
};

static const uint16_t blastSprite[16] = {
  0b0000000100000000,
  0b0010000100000100,
  0b0001001110001000,
  0b0000101010010000,
  0b0000011111100000,
  0b0100111001110010,
  0b0011110000111100,
  0b1111100000011111,
  0b0011110000111100,
  0b0100111001110010,
  0b0000011111100000,
  0b0000100101010000,
  0b0001000111001000,
  0b0010000100000100,
  0b0000000100000000,
  0b0000000000000000,
};

static const uint16_t ammoSprite[16] = {
  0, 0, 0, 0, 0, 0,
  0b0011111111111100,
  0b0010000000000100,
  0b0010110110110100,
  0b0010110110110100,
  0b0010110110110100,
  0b0010000000000100,
  0b0011111111111100,
  0b0011111111111100,
  0, 0,
};

static const uint16_t healthSprite[16] = {
  0, 0, 0, 0,
  0b0011111111111100,
  0b0010000000000100,
  0b0010000110000100,
  0b0010000110000100,
  0b0010011111100100,
  0b0010011111100100,
  0b0010000110000100,
  0b0010000110000100,
  0b0010000000000100,
  0b0011111111111100,
  0, 0,
};

static const uint16_t keySprite[16] = {
  0, 0, 0, 0, 0,
  0b0001111111111000,
  0b0001000000001000,
  0b0001011100001000,
  0b0001011100001000,
  0b0001000000001000,
  0b0001011111101000,
  0b0001000000001000,
  0b0001111111111000,
  0, 0, 0,
};

// ---------------------------------------------------------------- deck 1

// # panel  % brick  & tech  D door  L keycard door  E exit (a wall you use)
// P player (facing east)  d droid  a ammo  h medkit  k keycard
static const char *const deck1[mapSize] = {
  "################",
  "#P....#....a...#",
  "#.....D........#",
  "#.....#...d....#",
  "###D###........#",
  "#.....%%%%%%%D%#",
  "#..h..%........#",
  "#.....%..d.....#",
  "#.d...D........#",
  "#.....%.....k..#",
  "&&&L&&&&&&&&&&&&",
  "&..............&",
  "&..d......d....&",
  "&.......a......&",
  "&..............E",
  "&&&&&&&&&&&&&&&&",
};

// ---------------------------------------------------------------- state

static uint8_t deckMap[mapSize][mapSize];
static Thing things[maxThings];
static float posX, posY, dirX, dirY, planeX, planeY;
static float zBuffer[viewW];
static int hp, ammo;
static bool hasKey;
static int deck;
static uint32_t score;
static int kills;
static unsigned long lastFire, muzzleUntil, hurtUntil, messageUntil;
static char message[24];
static float bob;
static float tiltZero;

static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

static const uint16_t sfxGun[] = {900, 15, 420, 25, 160, 40, 0, 0};
static const uint16_t sfxEmpty[] = {2400, 10, 0, 0};
static const uint16_t sfxAlert[] = {1400, 40, 1900, 50, 0, 0};
static const uint16_t sfxDroidShot[] = {650, 20, 320, 30, 0, 0};
static const uint16_t sfxDoor[] = {180, 70, 240, 70, 300, 60, 0, 0};
static const uint16_t sfxLocked[] = {300, 80, 0, 40, 300, 80, 0, 0};

// ---------------------------------------------------------------- helpers

static inline void setPx(uint8_t *buf, int x, int y){
  buf[x + (y >> 3) * screenW] |= (1 << (y & 7));
}

static inline void clearPx(uint8_t *buf, int x, int y){
  buf[x + (y >> 3) * screenW] &= ~(1 << (y & 7));
}

static bool solid(int x, int y){
  if( x < 0 || y < 0 || x >= mapSize || y >= mapSize ) return true;
  return deckMap[y][x] != T_EMPTY;
}

static void showMessage(const char *text){
  strncpy(message, text, sizeof(message) - 1);
  message[sizeof(message) - 1] = '\0';
  messageUntil = gameMillis() + messageMs;
}

static void setAngle(float a){
  dirX = cosf(a);
  dirY = sinf(a);
  planeX = -dirY * fovPlane;
  planeY = dirX * fovPlane;
}

static void rotate(float a){
  float c = cosf(a), s = sinf(a);
  float dx = dirX * c - dirY * s;
  dirY = dirX * s + dirY * c;
  dirX = dx;
  float px = planeX * c - planeY * s;
  planeY = planeX * s + planeY * c;
  planeX = px;
}

static Thing *addThing(ThingType type, float x, float y){
  for( int i = 0; i < maxThings; i++ ){
    if( !things[i].alive ){
      things[i] = {true, type, x, y, (int8_t)(type == TH_DROID ? 2 + deck / 2 : 1), DS_IDLE, 0, 0, 0};
      return &things[i];
    }
  }
  return nullptr;
}

static bool lineOfSight(float x0, float y0, float x1, float y1){
  float dx = x1 - x0, dy = y1 - y0;
  float dist = sqrtf(dx * dx + dy * dy);
  int steps = (int)(dist / 0.15f);
  for( int i = 1; i < steps; i++ ){
    float f = i / (float)steps;
    if( solid((int)(x0 + dx * f), (int)(y0 + dy * f)) ) return false;
  }
  return true;
}

// ---------------------------------------------------------------- levels

static void loadDeck1(){
  for( int y = 0; y < mapSize; y++ ){
    for( int x = 0; x < mapSize; x++ ){
      char c = deck1[y][x];
      uint8_t t = T_EMPTY;
      switch( c ){
        case '#': t = T_PANEL; break;
        case '%': t = T_BRICK; break;
        case '&': t = T_TECH; break;
        case 'D': t = T_DOOR; break;
        case 'L': t = T_LOCKED; break;
        case 'E': t = T_EXIT; break;
        case 'P': posX = x + 0.5f; posY = y + 0.5f; setAngle(0); break;
        case 'd': addThing(TH_DROID, x + 0.5f, y + 0.5f); break;
        case 'a': addThing(TH_AMMO, x + 0.5f, y + 0.5f); break;
        case 'h': addThing(TH_HEALTH, x + 0.5f, y + 0.5f); break;
        case 'k': addThing(TH_KEY, x + 0.5f, y + 0.5f); break;
      }
      deckMap[y][x] = t;
    }
  }
}

// a random maze: cells sit on odd coordinates, walls and doors between them
static void generateDeck(){
  Maze m;
  const int cells = 7;
  m.generate(cells, cells);
  m.braid(45);
  for( int y = 0; y < mapSize; y++ ){
    for( int x = 0; x < mapSize; x++ ){
      deckMap[y][x] = 1 + ((x / 5 + y / 5 + deck) % 3);   // wall style changes by zone
    }
  }
  for( int cy = 0; cy < cells; cy++ ){
    for( int cx = 0; cx < cells; cx++ ){
      int gx = 2 * cx + 1, gy = 2 * cy + 1;
      deckMap[gy][gx] = T_EMPTY;
      if( cx < cells - 1 && m.open(cx, cy, 1) ) deckMap[gy][gx + 1] = random(100) < 15 ? T_DOOR : T_EMPTY;
      if( cy < cells - 1 && m.open(cx, cy, 2) ) deckMap[gy + 1][gx] = random(100) < 15 ? T_DOOR : T_EMPTY;
    }
  }

  // distances from the start cell (doors count as open)
  int dist[cells][cells];
  for( int y = 0; y < cells; y++ ) for( int x = 0; x < cells; x++ ) dist[y][x] = -1;
  int queue[cells * cells], head = 0, tail = 0;
  dist[0][0] = 0;
  queue[tail++] = 0;
  static const int8_t ddx[4] = {0, 1, 0, -1}, ddy[4] = {-1, 0, 1, 0};
  int far = 0;
  while( head < tail ){
    int c = queue[head++];
    int cx = c % cells, cy = c / cells;
    if( dist[cy][cx] > dist[far / cells][far % cells] ) far = c;
    for( int d = 0; d < 4; d++ ){
      int nx = cx + ddx[d], ny = cy + ddy[d];
      if( nx < 0 || ny < 0 || nx >= cells || ny >= cells || dist[ny][nx] >= 0 ) continue;
      if( deckMap[2 * cy + 1 + ddy[d]][2 * cx + 1 + ddx[d]] == T_EMPTY ||
          deckMap[2 * cy + 1 + ddy[d]][2 * cx + 1 + ddx[d]] == T_DOOR ){
        dist[ny][nx] = dist[cy][cx] + 1;
        queue[tail++] = ny * cells + nx;
      }
    }
  }

  // exit: a wall of the farthest cell that has one (loops can leave a cell
  // with no walls at all)
  bool exitPlaced = false;
  for( int want = dist[far / cells][far % cells]; want >= 0 && !exitPlaced; want-- ){
    for( int c = 0; c < cells * cells && !exitPlaced; c++ ){
      int cx = c % cells, cy = c / cells;
      if( dist[cy][cx] != want ) continue;
      for( int d = 0; d < 4; d++ ){
        int wx = 2 * cx + 1 + ddx[d], wy = 2 * cy + 1 + ddy[d];
        if( deckMap[wy][wx] != T_EMPTY && deckMap[wy][wx] != T_DOOR ){
          deckMap[wy][wx] = T_EXIT;
          exitPlaced = true;
          break;
        }
      }
    }
  }

  // player at the start, facing an open way
  posX = posY = 1.5f;
  setAngle(deckMap[1][2] == T_EMPTY || deckMap[1][2] == T_DOOR ? 0 : PI / 2);

  // droids away from the start, a few pickups anywhere
  int droids = min(3 + deck, 12);
  for( int i = 0, tries = 0; i < droids && tries < 200; tries++ ){
    int cx = random(cells), cy = random(cells);
    if( dist[cy][cx] >= 3 ){
      addThing(TH_DROID, 2 * cx + 1.5f, 2 * cy + 1.5f);
      i++;
    }
  }
  for( int i = 0; i < 3; i++ ) addThing(TH_AMMO, 2 * random(cells) + 1.5f, 2 * random(cells) + 1.5f);
  for( int i = 0; i < 2; i++ ) addThing(TH_HEALTH, 2 * random(cells) + 1.5f, 2 * random(cells) + 1.5f);
}

static void startDeck(){
  for( int i = 0; i < maxThings; i++ ) things[i].alive = false;
  hasKey = false;
  if( deck == 1 ) loadDeck1();
  else generateDeck();
  char buf[24];
  snprintf(buf, sizeof(buf), "DECK %d", deck);
  showMessage(buf);
}

// ---------------------------------------------------------------- game interface

static void derelictStart(){
  deck = 1;
  hp = 100;
  ammo = 24;
  score = 0;
  kills = 0;
  lastFire = 0;
  muzzleUntil = hurtUntil = messageUntil = 0;
  bob = 0;
  tiltZero = readTilt();
  startDeck();
}

static void derelictResume(){
  tiltZero = readTilt();
}

static void tryMove(float &x, float &y, float mx, float my, float r){
  float nx = x + mx;
  if( !solid((int)(nx + (mx > 0 ? r : -r)), (int)(y + r)) && !solid((int)(nx + (mx > 0 ? r : -r)), (int)(y - r)) ) x = nx;
  float ny = y + my;
  if( !solid((int)(x + r), (int)(ny + (my > 0 ? r : -r))) && !solid((int)(x - r), (int)(ny + (my > 0 ? r : -r))) ) y = ny;
}

// screen column, size and depth of a thing, false if behind the camera
static bool project(const Thing &t, float &screenX, float &size, float &depth){
  float sx = t.x - posX, sy = t.y - posY;
  float invDet = 1.0f / (planeX * dirY - dirX * planeY);
  float tx = invDet * (dirY * sx - dirX * sy);
  depth = invDet * (-planeY * sx + planeX * sy);
  if( depth < 0.15f ) return false;
  screenX = viewW / 2.0f * (1 + tx / depth);
  size = viewH / depth;
  return true;
}

static void fire(unsigned long now){
  if( now - lastFire < fireCooldownMs ) return;
  lastFire = now;
  if( ammo <= 0 ){
    playSfx(sfxEmpty);
    showMessage("NO AMMO");
    return;
  }
  ammo--;
  muzzleUntil = now + 70;
  playSfx(sfxGun);

  // hitscan: the nearest droid under the crosshair that is in front of the wall
  Thing *target = nullptr;
  float targetDepth = zBuffer[viewW / 2];
  for( int i = 0; i < maxThings; i++ ){
    Thing &t = things[i];
    if( !t.alive || t.type != TH_DROID || t.state == DS_DYING ) continue;
    float sx, size, depth;
    if( !project(t, sx, size, depth) ) continue;
    if( fabsf(sx - viewW / 2) < size * 0.3f && depth < targetDepth ){
      target = &t;
      targetDepth = depth;
    }
  }
  if( target ){
    target->hp--;
    target->state = DS_CHASE;   // shooting wakes it up
    target->flashUntil = now + 80;
    if( target->hp <= 0 ){
      target->state = DS_DYING;
      target->dyingSince = now;
      kills++;
      score += 100;
      playSfx(sfxBoom);
    }
  }
}

static void useAhead(){
  int tx = (int)(posX + dirX * 0.9f), ty = (int)(posY + dirY * 0.9f);
  if( tx < 0 || ty < 0 || tx >= mapSize || ty >= mapSize ) return;
  uint8_t &t = deckMap[ty][tx];
  if( t == T_DOOR ){
    t = T_EMPTY;
    playSfx(sfxDoor);
  } else if( t == T_LOCKED ){
    if( hasKey ){
      t = T_EMPTY;
      playSfx(sfxDoor);
      showMessage("ACCESS GRANTED");
    } else {
      playSfx(sfxLocked);
      showMessage("NEED KEYCARD");
    }
  } else if( t == T_EXIT ){
    score += 500 + hp * 2;
    deck++;
    playSfx(sfxLevel);
    startDeck();
  }
}

static bool derelictUpdate(const Buttons &held, const Buttons &pressed){
  unsigned long now = gameMillis();

  // turning: tilt, or the side pads when there is no IMU
  float tilt = readTilt() - tiltZero;
  if( fabsf(tilt) > tiltDeadzone ){
    rotate((tilt - (tilt > 0 ? tiltDeadzone : -tiltDeadzone)) * tiltTurnGain);
  }
  bool padsTurn = !imuAvailable();
  if( padsTurn && held.left ) rotate(-turnSpeed);
  if( padsTurn && held.right ) rotate(turnSpeed);

  float mx = 0, my = 0;
  if( held.up ){ mx += dirX; my += dirY; }
  if( held.down ){ mx -= dirX; my -= dirY; }
  if( !padsTurn && held.left ){ mx += dirY; my -= dirX; }    // strafe left
  if( !padsTurn && held.right ){ mx -= dirY; my += dirX; }   // strafe right
  float len = sqrtf(mx * mx + my * my);
  if( len > 0 ){
    tryMove(posX, posY, mx / len * moveSpeed, my / len * moveSpeed, playerRadius);
    bob += 0.35f;
  }

  if( pressed.fire ) fire(now);
  if( pressed.shield ) useAhead();

  // things
  for( int i = 0; i < maxThings; i++ ){
    Thing &t = things[i];
    if( !t.alive ) continue;
    float dx = posX - t.x, dy = posY - t.y;
    float dist = sqrtf(dx * dx + dy * dy);

    if( t.type != TH_DROID ){
      if( dist < 0.55f ){
        if( t.type == TH_AMMO ){
          ammo = min(60, ammo + 10);
          showMessage("+10 AMMO");
        } else if( t.type == TH_HEALTH ){
          if( hp >= 100 ) continue;     // leave it for later
          hp = min(100, hp + 25);
          showMessage("+25 HEALTH");
        } else {
          hasKey = true;
          showMessage("KEYCARD FOUND");
        }
        t.alive = false;
        playSfx(sfxPickup);
      }
      continue;
    }

    if( t.state == DS_DYING ){
      if( now - t.dyingSince > 350 ){
        t.alive = false;
        if( random(100) < 35 ) addThing(TH_AMMO, t.x, t.y);
      }
      continue;
    }

    bool sees = dist < 9 && lineOfSight(t.x, t.y, posX, posY);
    if( t.state == DS_IDLE && sees ){
      t.state = DS_CHASE;
      t.nextShot = now + 700;
      playSfx(sfxAlert);
    }
    if( t.state == DS_CHASE ){
      if( dist > 1.6f ){
        float speed = 0.025f + deck * 0.003f;
        tryMove(t.x, t.y, dx / dist * speed, dy / dist * speed, 0.3f);
      }
      if( sees && now >= t.nextShot ){
        t.nextShot = now + 900 + random(900);
        t.flashUntil = now + 120;
        playSfx(sfxDroidShot);
        float chance = constrain(0.85f - dist * 0.08f, 0.25f, 0.85f);
        if( random(1000) < chance * 1000 ){
          hp -= 6 + random(7) + deck;
          hurtUntil = now + 100;
          playSfx(sfxHit);
          if( hp <= 0 ){
            hp = 0;
            return false;
          }
        }
      }
    }
  }

  rgbLed(1, now < hurtUntil || (hp < 25 && (now / 300) % 2));
  rgbLed(3, now < muzzleUntil);
  return true;
}

// ---------------------------------------------------------------- rendering

static bool wallPattern(uint8_t tile, int tx, int ty){
  // true = dark seam / detail pixel on a 16x16 texture
  switch( tile ){
    case T_PANEL: return tx == 0 || ty == 0 || ((tx == 2 || tx == 13) && (ty == 2 || ty == 13));
    case T_BRICK: return ty % 4 == 0 || (tx + ((ty / 4) % 2) * 4) % 8 == 0;
    case T_TECH:  return tx % 8 == 0 || ty == 8 || (ty > 2 && ty < 6 && tx % 8 > 1 && tx % 8 < 6 && (tx + ty) % 2);
    case T_DOOR:  return tx == 0 || tx == 15 || ty == 0 || (tx % 4 == 2 && ty > 2 && ty < 14);
    case T_LOCKED: return tx == 0 || tx == 15 || ty == 0 || ((tx % 4 == 2 && ty > 2 && ty < 14) && !(tx > 5 && tx < 10 && ty > 5 && ty < 10));
    case T_EXIT:  return ((tx / 4 + ty / 4) % 2) == ((millis() / 300) % 2);
  }
  return false;
}

static void drawSprite(uint8_t *buf, const uint16_t *bits, float screenX, float size, float depth, float scale, float drop){
  float s = size * scale;
  int left = (int)(screenX - s / 2), right = (int)(screenX + s / 2);
  int top = (int)(viewH / 2 - s / 2 + drop * size), bottom = (int)(top + s);
  for( int x = max(0, left); x < min(viewW, right); x++ ){
    if( depth >= zBuffer[x] ) continue;
    int tx = (int)((x - left) * 16 / s);
    if( tx < 0 || tx > 15 ) continue;
    for( int y = max(0, top); y < min(viewH, bottom); y++ ){
      int ty = (int)((y - top) * 16 / s);
      if( ty < 0 || ty > 15 ) continue;
      uint16_t row = bits[ty];
      if( row == 0 ) continue;
      bool on = (row >> (15 - tx)) & 1;
      if( on ){
        setPx(buf, x, y);
      } else {
        // black inside the silhouette so sprites stand out from walls
        int first = __builtin_clz((uint32_t)row) - 16;
        int last = 15 - __builtin_ctz((uint32_t)row);
        if( tx > first && tx < last ) clearPx(buf, x, y);
      }
    }
  }
}

static void derelictDraw(){
  unsigned long now = gameMillis();
  display.clearDisplay();
  uint8_t *buf = display.getBuffer();

  // floor: gets brighter towards your feet
  for( int y = viewH / 2 + 2; y < viewH; y++ ){
    int level = (y - viewH / 2) * 5 / (viewH / 2);
    for( int x = 0; x < viewW; x++ ){
      if( bayer[y & 3][x & 3] < level ) setPx(buf, x, y);
    }
  }

  // walls, one ray per column
  for( int x = 0; x < viewW; x++ ){
    float camX = 2.0f * x / viewW - 1;
    float rdx = dirX + planeX * camX, rdy = dirY + planeY * camX;
    int mx = (int)posX, my = (int)posY;
    float ddx = rdx == 0 ? 1e30f : fabsf(1 / rdx);
    float ddy = rdy == 0 ? 1e30f : fabsf(1 / rdy);
    int stepX = rdx < 0 ? -1 : 1, stepY = rdy < 0 ? -1 : 1;
    float sdx = rdx < 0 ? (posX - mx) * ddx : (mx + 1 - posX) * ddx;
    float sdy = rdy < 0 ? (posY - my) * ddy : (my + 1 - posY) * ddy;
    int side = 0;
    uint8_t tile = T_PANEL;
    for( int i = 0; i < 64; i++ ){
      if( sdx < sdy ){ sdx += ddx; mx += stepX; side = 0; }
      else { sdy += ddy; my += stepY; side = 1; }
      if( solid(mx, my) ){
        tile = (mx < 0 || my < 0 || mx >= mapSize || my >= mapSize) ? (uint8_t)T_PANEL : deckMap[my][mx];
        break;
      }
    }
    float perp = side == 0 ? sdx - ddx : sdy - ddy;
    perp = max(perp, 0.05f);
    zBuffer[x] = perp;

    float wallX = side == 0 ? posY + perp * rdy : posX + perp * rdx;
    wallX -= floorf(wallX);
    int tx = (int)(wallX * 16) & 15;

    int lineH = (int)(viewH / perp);
    int top = viewH / 2 - lineH / 2;
    int y0 = max(0, top), y1 = min(viewH - 1, top + lineH);
    // distance shading, x-facing walls a bit darker
    int level = (int)(17 - perp * 2.4f);
    if( side == 1 ) level = level * 2 / 3;
    if( tile == T_EXIT ) level = 16;
    level = constrain(level, 1, 16);
    for( int y = y0; y <= y1; y++ ){
      int ty = ((y - top) * 16 / max(1, lineH)) & 15;
      if( !wallPattern(tile, tx, ty) && bayer[y & 3][x & 3] < level ) setPx(buf, x, y);
      else clearPx(buf, x, y);
    }
  }

  // things, far to near
  int order[maxThings], n = 0;
  float depths[maxThings];
  for( int i = 0; i < maxThings; i++ ){
    if( !things[i].alive ) continue;
    float dx = things[i].x - posX, dy = things[i].y - posY;
    depths[i] = dx * dx + dy * dy;
    int j = n++;
    while( j > 0 && depths[order[j - 1]] < depths[i] ){ order[j] = order[j - 1]; j--; }
    order[j] = i;
  }
  for( int k = 0; k < n; k++ ){
    const Thing &t = things[order[k]];
    float sx, size, depth;
    if( !project(t, sx, size, depth) ) continue;
    switch( t.type ){
      case TH_DROID: {
        const uint16_t *bits = t.state == DS_DYING ? blastSprite : now < t.flashUntil ? droidFiringSprite : droidSprite;
        float hover = 0.04f * sinf(now / 250.0f + order[k]);
        drawSprite(buf, bits, sx, size, depth, 0.7f, hover);
        break;
      }
      case TH_AMMO:   drawSprite(buf, ammoSprite, sx, size, depth, 0.45f, 0.25f); break;
      case TH_HEALTH: drawSprite(buf, healthSprite, sx, size, depth, 0.45f, 0.25f); break;
      case TH_KEY:    drawSprite(buf, keySprite, sx, size, depth, 0.4f, 0.27f); break;
    }
  }

  // gun with a little walking bob, muzzle flash when firing
  int gb = (int)(fabsf(sinf(bob)) * 2);
  display.fillRect(58, 45 + gb, 13, viewH - 45 - gb, SSD1306_BLACK);
  display.fillRect(62, 44 + gb, 5, 9, SSD1306_WHITE);
  display.fillRect(59, 51 + gb, 11, viewH - 51 - gb, SSD1306_WHITE);
  display.drawFastVLine(64, 45 + gb, 6, SSD1306_BLACK);
  if( now < muzzleUntil ){
    display.fillTriangle(64, 34, 59, 43, 69, 43, SSD1306_WHITE);
    display.fillCircle(64, 40, 3, SSD1306_WHITE);
  }

  // crosshair with a dark outline so it shows on any wall
  for( int d = 2; d <= 4; d++ ){
    display.drawPixel(64 - d, viewH / 2, SSD1306_INVERSE);
    display.drawPixel(64 + d, viewH / 2, SSD1306_INVERSE);
    display.drawPixel(64, viewH / 2 - d, SSD1306_INVERSE);
    display.drawPixel(64, viewH / 2 + d, SSD1306_INVERSE);
  }

  if( now < messageUntil ){
    int w = strlen(message) * 6 + 6;
    display.fillRect(64 - w / 2, 1, w, 10, SSD1306_BLACK);
    display.drawRect(64 - w / 2, 1, w, 10, SSD1306_WHITE);
    centerText(message, 2);
  }

  // HUD
  display.fillRect(0, viewH, screenW, screenH - viewH, SSD1306_BLACK);
  display.drawFastHLine(0, viewH, screenW, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, viewH + 1);
  display.printf("HP%-3d AM%-2d %s D%d", hp, ammo, hasKey ? "KEY" : "   ", deck);

  display.invertDisplay(now < hurtUntil);
}

static uint32_t derelictScore(){
  return score;
}

static void derelictSummary(char *buf, size_t len){
  snprintf(buf, len, "deck %d kills %d", deck, kills);
}

extern const Game derelictGame = {
  "DERELICT", "derelict", "tilt to turn, shoot",
  derelictStart, derelictUpdate, derelictDraw, derelictScore, derelictSummary, derelictResume,
};
