/* ORBITAL DUEL - two players on two CADSE boards over ESP-NOW
 *
 * Lobby:  every board in the duel lobby shows up with its id.
 *         Touch Up/Down to pick one, X to invite, touch Left to leave.
 *         When invited: X accepts, touch Left declines.
 * Duel:   you are always on the left, your opponent on the right.
 *         Tilt or touch Up/Down to move, left bumper fires,
 *         right bumper (hold) raises a shield. First to 5 hits wins.
 *
 * Sync:   each board decides itself whether it was hit (and tells the
 *         other), shots and asteroids are computed from a shared match
 *         clock, so a lost packet only delays an update instead of
 *         breaking the game.
 */
#include "../game.h"
#include "../net.h"

const int duelHudH = 10;
const int myShipX = 8;
const int oppShipX = screenW - 9;
const int duelMinY = duelHudH + 4;
const int duelMaxY = screenH - 5;
const int duelMaxShots = 3;
const int duelWinHits = 5;
const int maxPeers = 6;
const int numAsteroids = 3;

const float shotSpeed = 0.085f;            // px per ms
const unsigned long fireCooldownMs = 350;
const float duelShieldDrain = 2.0f;
const float duelShieldRecharge = 0.25f;
const float duelTiltGain = 0.6f;
const float duelTouchSpeed = 2.2f;
const unsigned long helloEveryMs = 400;
const unsigned long inviteEveryMs = 300;
const unsigned long stateEveryMs = 40;
const unsigned long peerTimeoutMs = 2500;
const unsigned long linkTimeoutMs = 3000;
const unsigned long countdownMs = 3000;
const unsigned long resultMs = 2500;

enum DuelPhase { LOBBY, INVITING, INVITED, COUNTDOWN, FIGHT, RESULT };
enum PacketType : uint8_t { PKT_HELLO = 1, PKT_INVITE, PKT_ACCEPT, PKT_DECLINE, PKT_STATE };

struct __attribute__((packed)) PacketHeader {
  char magic[2];
  uint8_t version;
  uint8_t type;
  uint16_t from, to;
};

struct __attribute__((packed)) AcceptPacket {
  PacketHeader h;
  uint32_t seed;
  uint16_t msUntilStart;
};

struct __attribute__((packed)) ShotInfo {
  uint8_t id;          // 0 = slot unused
  uint8_t y;
  uint32_t firedAt;    // match time in ms
};

struct __attribute__((packed)) StatePacket {
  PacketHeader h;
  uint8_t y;
  uint8_t shield;
  uint8_t hitsTaken;
  ShotInfo shots[duelMaxShots];
  uint8_t dead[4];     // ids of the opponent's shots that hit or were blocked here
};

struct Peer {
  uint16_t id;
  unsigned long lastSeen;
};

struct Asteroid {
  float x, phase, speed;
  int r, dir;
};

static DuelPhase phase;
static unsigned long phaseSince;
static Peer peers[maxPeers];
static int numPeers;
static int selectedPeer;
static uint16_t opponent;
static bool isHost;
static unsigned long matchStart;
static unsigned long lastHello, lastInvite, lastState, lastHeard, lastAccept;
static bool hostConfirmed;
static uint32_t matchSeed;
static char message[24];
static unsigned long messageUntil;
static char result[12];

static float myY;
static float shieldEnergy;
static bool shieldOn;
static int hitsTaken;
static ShotInfo myShots[duelMaxShots];
static uint8_t nextShotId;
static unsigned long lastFire;
static uint8_t myDead[4];
static int myDeadPos;
static unsigned long flashUntil;

static float oppY;
static bool oppShield;
static int oppHitsTaken;
static ShotInfo oppShots[duelMaxShots];
static bool oppShotDone[256];   // opponent shot ids we already handled
static Asteroid asteroids[numAsteroids];
static float zeroY;

// ---------------------------------------------------------------- packets

static void fillHeader(PacketHeader &h, PacketType type, uint16_t to){
  h.magic[0] = 'C';
  h.magic[1] = 'A';
  h.version = 1;
  h.type = type;
  h.from = netId();
  h.to = to;
}

static void sendSimple(PacketType type, uint16_t to){
  PacketHeader h;
  fillHeader(h, type, to);
  netSend(&h, sizeof(h));
}

static long matchTime(){
  return (long)gameMillis() - (long)matchStart;
}

static void sendAccept(){
  AcceptPacket p;
  fillHeader(p.h, PKT_ACCEPT, opponent);
  p.seed = matchSeed;
  long left = -matchTime();
  p.msUntilStart = left > 0 ? left : 0;
  netSend(&p, sizeof(p));
}

static void sendState(){
  StatePacket p;
  fillHeader(p.h, PKT_STATE, opponent);
  p.y = (uint8_t)myY;
  p.shield = shieldOn;
  p.hitsTaken = hitsTaken;
  memcpy(p.shots, myShots, sizeof(myShots));
  memcpy(p.dead, myDead, sizeof(myDead));
  netSend(&p, sizeof(p));
}

static void setPhase(DuelPhase p){
  phase = p;
  phaseSince = gameMillis();
}

static void showMessage(const char *text){
  strncpy(message, text, sizeof(message) - 1);
  message[sizeof(message) - 1] = '\0';
  messageUntil = gameMillis() + 2000;
}

// both boards derive the same asteroids from the shared seed
static uint32_t nextRandom(uint32_t &s){
  s = s * 1664525u + 1013904223u;
  return s >> 8;
}

static void startCountdown(unsigned long msUntilStart){
  uint32_t s = matchSeed;
  const float xs[numAsteroids] = {44, 64, 84};
  for( int i = 0; i < numAsteroids; i++ ){
    asteroids[i].x = isHost ? xs[i] : screenW - 1 - xs[i];   // the guest sees a mirrored arena
    asteroids[i].phase = nextRandom(s) % 48;
    asteroids[i].speed = 8 + nextRandom(s) % 14;
    asteroids[i].r = 3 + nextRandom(s) % 3;
    asteroids[i].dir = (i % 2) ? 1 : -1;
  }

  myY = oppY = (duelMinY + duelMaxY) / 2;
  shieldEnergy = 100;
  shieldOn = oppShield = false;
  hitsTaken = oppHitsTaken = 0;
  memset(myShots, 0, sizeof(myShots));
  memset(oppShots, 0, sizeof(oppShots));
  memset(myDead, 0, sizeof(myDead));
  memset(oppShotDone, 0, sizeof(oppShotDone));
  myDeadPos = 0;
  nextShotId = 1;
  lastFire = 0;
  flashUntil = 0;
  hostConfirmed = isHost;
  float tx;
  readTilt2(tx, zeroY);

  matchStart = gameMillis() + msUntilStart;
  lastHeard = gameMillis();
  setPhase(COUNTDOWN);
  playSfx(sfxStart);
}

static void rememberPeer(uint16_t id){
  unsigned long now = gameMillis();
  for( int i = 0; i < numPeers; i++ ){
    if( peers[i].id == id ){
      peers[i].lastSeen = now;
      return;
    }
  }
  if( numPeers < maxPeers ){
    peers[numPeers++] = {id, now};
    playSfx(sfxBlip);
  }
}

static void forgetOldPeers(){
  unsigned long now = gameMillis();
  for( int i = 0; i < numPeers; ){
    if( now - peers[i].lastSeen > peerTimeoutMs ){
      peers[i] = peers[--numPeers];
    } else {
      i++;
    }
  }
  if( selectedPeer >= numPeers ){
    selectedPeer = max(0, numPeers - 1);
  }
}

static void handlePacket(const uint8_t *buf, int len){
  if( len < (int)sizeof(PacketHeader) ){
    return;
  }
  const PacketHeader &h = *(const PacketHeader *)buf;
  if( h.magic[0] != 'C' || h.magic[1] != 'A' || h.version != 1 || h.from == netId() ){
    return;
  }
  bool toMe = h.to == netId();

  switch( h.type ){
    case PKT_HELLO:
      rememberPeer(h.from);
      break;

    case PKT_INVITE:
      if( !toMe ) break;
      // if both invite each other, the higher id gives in and gets asked to accept
      if( phase == LOBBY || (phase == INVITING && h.from == opponent && netId() > h.from) ){
        opponent = h.from;
        setPhase(INVITED);
        playSfx(sfxLevel);
      }
      break;

    case PKT_ACCEPT:
      if( toMe && phase == INVITING && h.from == opponent && len >= (int)sizeof(AcceptPacket) ){
        const AcceptPacket &p = *(const AcceptPacket *)buf;
        matchSeed = p.seed;
        isHost = true;
        startCountdown(p.msUntilStart);
      }
      break;

    case PKT_DECLINE:
      if( toMe && phase == INVITING && h.from == opponent ){
        showMessage("invite declined");
        setPhase(LOBBY);
      }
      break;

    case PKT_STATE:
      if( toMe && h.from == opponent && (phase == COUNTDOWN || phase == FIGHT || phase == RESULT) &&
          len >= (int)sizeof(StatePacket) ){
        const StatePacket &p = *(const StatePacket *)buf;
        oppY = p.y;
        oppShield = p.shield;
        oppHitsTaken = p.hitsTaken;
        memcpy(oppShots, p.shots, sizeof(oppShots));
        // shot ids wrap after 255, so forget the ones half a lap behind
        for( int s = 0; s < duelMaxShots; s++ ){
          if( p.shots[s].id ){
            oppShotDone[(uint8_t)(p.shots[s].id + 128)] = false;
          }
        }
        // our shots the opponent reported as hit or blocked
        for( int i = 0; i < 4; i++ ){
          for( int s = 0; s < duelMaxShots; s++ ){
            if( p.dead[i] && myShots[s].id == p.dead[i] ){
              myShots[s].id = 0;
            }
          }
        }
        lastHeard = gameMillis();
        hostConfirmed = true;
      }
      break;
  }
}

// ---------------------------------------------------------------- arena

static float asteroidY(const Asteroid &a, long t){
  float y = fmodf(a.phase + a.dir * a.speed * t / 1000.0f, 48.0f);
  if( y < 0 ) y += 48;
  return duelHudH + 3 + y;
}

static bool hitsAsteroid(float x, float y, long t){
  for( int i = 0; i < numAsteroids; i++ ){
    const Asteroid &a = asteroids[i];
    if( fabsf(x - a.x) <= a.r + 1 && fabsf(y - asteroidY(a, t)) <= a.r + 1 ){
      return true;
    }
  }
  return false;
}

static float myShotX(const ShotInfo &s, long t){
  return myShipX + 4 + shotSpeed * (t - (long)s.firedAt);
}

static float oppShotX(const ShotInfo &s, long t){
  return oppShipX - 4 - shotSpeed * (t - (long)s.firedAt);
}

static void markOppShotDone(uint8_t id){
  oppShotDone[id] = true;
}

static void reportDead(uint8_t id){
  myDead[myDeadPos] = id;
  myDeadPos = (myDeadPos + 1) % 4;
}

static void updateFight(const Buttons &held, const Buttons &pressed){
  long t = matchTime();
  unsigned long now = gameMillis();

  float tx, ty;
  readTilt2(tx, ty);
  float v = (ty - zeroY) * duelTiltGain;
  if( held.up ) v -= duelTouchSpeed;
  if( held.down ) v += duelTouchSpeed;
  myY = constrain(myY + v, (float)duelMinY, (float)duelMaxY);

  shieldOn = held.shield && shieldEnergy > 0;
  shieldEnergy = shieldOn ? max(0.0f, shieldEnergy - duelShieldDrain)
                          : min(100.0f, shieldEnergy + duelShieldRecharge);

  if( pressed.fire && now - lastFire > fireCooldownMs ){
    for( int s = 0; s < duelMaxShots; s++ ){
      if( myShots[s].id == 0 ){
        myShots[s] = {nextShotId, (uint8_t)myY, (uint32_t)max(0L, t)};
        nextShotId = (nextShotId == 255) ? 1 : nextShotId + 1;
        lastFire = now;
        playSfx(sfxBlip);
        break;
      }
    }
  }

  // our shots: gone when they leave the screen or hit an asteroid
  for( int s = 0; s < duelMaxShots; s++ ){
    if( myShots[s].id == 0 ) continue;
    float x = myShotX(myShots[s], t);
    if( x > screenW || hitsAsteroid(x, myShots[s].y, t) ){
      myShots[s].id = 0;
    }
  }

  // opponent shots: we decide whether they hit us
  for( int s = 0; s < duelMaxShots; s++ ){
    const ShotInfo &shot = oppShots[s];
    if( shot.id == 0 || oppShotDone[shot.id] ) continue;
    float x = oppShotX(shot, t);
    if( x < 0 || hitsAsteroid(x, shot.y, t) ){
      markOppShotDone(shot.id);
      continue;
    }
    if( fabsf(x - myShipX) < 5 && fabsf(shot.y - myY) < 5 ){
      markOppShotDone(shot.id);
      reportDead(shot.id);
      if( shieldOn ){
        playSfx(sfxClick);
      } else {
        hitsTaken++;
        flashUntil = now + 150;
        playSfx(sfxHit);
      }
    }
  }

  if( hitsTaken >= duelWinHits || oppHitsTaken >= duelWinHits ){
    bool won = oppHitsTaken >= duelWinHits && hitsTaken < duelWinHits;
    snprintf(result, sizeof(result), "%s %d-%d", won ? "WIN" : "LOSE", oppHitsTaken, hitsTaken);
    playSfx(won ? sfxLevel : sfxGameOver);
    setPhase(RESULT);
  }

  rgbLed(1, now < flashUntil);
  rgbLed(3, shieldOn);
}

// ---------------------------------------------------------------- game interface

static void duelStart(){
  numPeers = 0;
  selectedPeer = 0;
  opponent = 0;
  lastHello = lastInvite = lastState = lastAccept = 0;
  message[0] = '\0';
  messageUntil = 0;
  strcpy(result, "NO MATCH");
  setPhase(LOBBY);
  if( !netBegin() ){
    showMessage("radio failed");
  }
}

static bool duelUpdate(const Buttons &held, const Buttons &pressed){
  unsigned long now = gameMillis();

  uint8_t buf[netMaxPacket];
  int len;
  while( netReceive(buf, len) ){
    handlePacket(buf, len);
  }
  forgetOldPeers();

  bool inLobby = phase == LOBBY || phase == INVITING || phase == INVITED;
  if( inLobby && now - lastHello > helloEveryMs ){
    lastHello = now;
    sendSimple(PKT_HELLO, 0);
  }

  switch( phase ){
    case LOBBY:
      if( pressed.up && numPeers ) selectedPeer = (selectedPeer + numPeers - 1) % numPeers;
      if( pressed.down && numPeers ) selectedPeer = (selectedPeer + 1) % numPeers;
      if( (pressed.x || pressed.fire) && numPeers ){
        opponent = peers[selectedPeer].id;
        setPhase(INVITING);
        playSfx(sfxClick);
      }
      if( pressed.left ){
        return false;
      }
      break;

    case INVITING:
      if( now - lastInvite > inviteEveryMs ){
        lastInvite = now;
        sendSimple(PKT_INVITE, opponent);
      }
      if( pressed.left || now - phaseSince > 15000 ){
        if( !pressed.left ) showMessage("no answer");
        setPhase(LOBBY);
      }
      break;

    case INVITED:
      if( pressed.x || pressed.fire ){
        matchSeed = esp_random();
        isHost = false;
        startCountdown(countdownMs);
        sendAccept();
        lastAccept = now;
      } else if( pressed.left ){
        sendSimple(PKT_DECLINE, opponent);
        setPhase(LOBBY);
      } else if( now - phaseSince > 15000 ){
        setPhase(LOBBY);
      }
      break;

    case COUNTDOWN:
    case FIGHT:
      // the guest repeats its accept until the host answers
      if( !hostConfirmed && now - lastAccept > 200 ){
        lastAccept = now;
        sendAccept();
      }
      if( phase == COUNTDOWN && matchTime() >= 0 ){
        setPhase(FIGHT);
        playSfx(sfxBlip);
      }
      if( phase == FIGHT ){
        updateFight(held, pressed);
      }
      if( now - lastHeard > linkTimeoutMs ){
        strcpy(result, "LINK LOST");
        playSfx(sfxGameOver);
        setPhase(RESULT);
      }
      break;

    case RESULT:
      if( now - phaseSince > resultMs ){
        rgbLed(1, false);
        rgbLed(3, false);
        return false;
      }
      break;
  }

  // keep the opponent updated, also during the result so they see the end
  if( (phase == COUNTDOWN || phase == FIGHT || phase == RESULT) && now - lastState > stateEveryMs ){
    lastState = now;
    sendState();
  }
  return true;
}

static void drawShipFacing(int x, int y, int dir, bool shield){
  display.fillTriangle(x - 3 * dir, y - 3, x - 3 * dir, y + 3, x + 4 * dir, y, SSD1306_WHITE);
  display.drawPixel(x - 4 * dir, y, SSD1306_WHITE);
  if( shield ){
    display.drawCircle(x, y, 7, SSD1306_WHITE);
  }
}

static void drawArena(){
  long t = matchTime();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("YOU %d", oppHitsTaken);
  char right[12];
  snprintf(right, sizeof(right), "%d #%04X", hitsTaken, opponent);
  display.setCursor(screenW - strlen(right) * 6, 0);
  display.print(right);
  display.drawRect(50, 2, 28, 5, SSD1306_WHITE);
  display.fillRect(51, 3, (int)(shieldEnergy * 26 / 100), 3, SSD1306_WHITE);
  display.drawFastHLine(0, duelHudH - 1, screenW, SSD1306_WHITE);

  for( int i = 0; i < numAsteroids; i++ ){
    display.drawCircle((int)asteroids[i].x, (int)asteroidY(asteroids[i], t), asteroids[i].r, SSD1306_WHITE);
  }
  for( int s = 0; s < duelMaxShots; s++ ){
    if( myShots[s].id ){
      int x = (int)myShotX(myShots[s], t);
      display.drawFastHLine(x - 3, myShots[s].y, 4, SSD1306_WHITE);
    }
    if( oppShots[s].id && !oppShotDone[oppShots[s].id] ){
      int x = (int)oppShotX(oppShots[s], t);
      display.drawFastHLine(x, oppShots[s].y, 4, SSD1306_WHITE);
      display.drawPixel(x, oppShots[s].y - 1, SSD1306_WHITE);
      display.drawPixel(x, oppShots[s].y + 1, SSD1306_WHITE);
    }
  }
  drawShipFacing(myShipX, (int)myY, 1, shieldOn);
  drawShipFacing(oppShipX, (int)oppY, -1, oppShield);
  display.invertDisplay(gameMillis() < flashUntil);
}

static void drawLobby(){
  char line[24];
  display.fillRect(0, 0, screenW, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print("ORBITAL DUEL");
  display.setCursor(screenW - 32, 1);
  display.printf("#%04X", netId());
  display.setTextColor(SSD1306_WHITE);

  if( phase == INVITING ){
    snprintf(line, sizeof(line), "calling #%04X", opponent);
    centerText(line, 22);
    int dots = (millis() / 300) % 4;
    centerText(dots == 0 ? "" : dots == 1 ? "." : dots == 2 ? ".." : "...", 32);
    centerText("< cancel", 54);
    return;
  }
  if( phase == INVITED ){
    snprintf(line, sizeof(line), "#%04X", opponent);
    centerText(line, 16, 2);
    centerText("wants to duel!", 34);
    centerText("X accept  < no", 54);
    return;
  }

  if( numPeers == 0 ){
    centerText("looking for boards", 22);
    int dots = (millis() / 300) % 4;
    centerText(dots == 0 ? "" : dots == 1 ? "." : dots == 2 ? ".." : "...", 32);
  } else {
    for( int i = 0; i < numPeers && i < 4; i++ ){
      int y = 12 + i * 10;
      if( i == selectedPeer ){
        display.fillRect(0, y, screenW, 10, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      }
      display.setCursor(4, y + 1);
      display.printf("board #%04X", peers[i].id);
      display.setTextColor(SSD1306_WHITE);
    }
  }
  if( gameMillis() < messageUntil ){
    centerText(message, 44);
  }
  centerText(numPeers ? "X invite  < quit" : "< quit", 55);
}

static void duelDraw(){
  display.clearDisplay();
  switch( phase ){
    case LOBBY:
    case INVITING:
    case INVITED:
      display.invertDisplay(false);
      drawLobby();
      break;

    case COUNTDOWN: {
      drawArena();
      long left = -matchTime();
      char n[12];
      snprintf(n, sizeof(n), "%ld", left / 1000 + 1);
      display.fillRect(52, 24, 24, 20, SSD1306_BLACK);
      centerText(n, 27, 2);
      break;
    }

    case FIGHT:
      drawArena();
      break;

    case RESULT:
      display.invertDisplay(false);
      drawArena();
      display.fillRect(14, 22, 100, 22, SSD1306_BLACK);
      display.drawRect(14, 22, 100, 22, SSD1306_WHITE);
      centerText(result, 26, 2);
      break;
  }
}

static uint32_t duelScore(){
  return oppHitsTaken;
}

static void duelSummary(char *buf, size_t len){
  snprintf(buf, len, "%s", result);
}

extern const Game duelGame = {
  "ORBITAL DUEL 2P", "duel", "2 boards, by radio",
  duelStart, duelUpdate, duelDraw, duelScore, duelSummary, nullptr, true, true,
};
