/* PULSE CROSS - rhythm game that plays a melody when you hit the notes
 *
 * Notes fly from the four sides towards the ring in the middle, matching the
 * touch-pad cross; X notes are rings shrinking onto the centre. Touch the
 * matching pad as a note reaches the ring and the synth plays it. The melody
 * is generated from a scale, and the direction follows it: a step up comes
 * from above, a step down from below, a repeat is an X ring, leaps come from
 * the right (up) or left (down).
 * Misses and stray taps drain the energy bar; every 24 notes the tempo rises.
 */
#include "../game.h"
#include "../music.h"

const int pcCenterX = 64;
const int pcCenterY = 37;
const int pcRing = 8;
const int maxChartNotes = 24;
const unsigned long perfectMs = 70;
const unsigned long goodMs = 150;
const unsigned long strayMs = 260;      // a tap this far from any note is a stray
const int notesPerLevel = 24;

enum Lane { LANE_UP, LANE_RIGHT, LANE_DOWN, LANE_LEFT, LANE_X };

struct ChartNote {
  bool used;
  bool done;
  unsigned long t;      // game time when it should be hit
  uint8_t lane;
  int midi;
};

static ChartNote chart[maxChartNotes];
static unsigned long nextStepAt;
static int degree;                      // melody position in the scale
static int scaleIdx;
static int level;
static int bpm;
static unsigned long travelMs;
static uint32_t score;
static int combo, bestCombo;
static float energy;
static int notesPassed;
static int perfects, goods, misses;
static const char *judgement;
static unsigned long judgedAt;
static unsigned long ringFlashUntil;
static uint16_t hitSfx[4];
static const uint16_t sfxMissNote[] = {110, 120, 0, 0};

static void applyLevel(){
  bpm = min(170, 92 + (level - 1) * 10);
  travelMs = max(800UL, 1700UL - (unsigned long)(level - 1) * 110);
}

static void pulseStart(){
  for( int i = 0; i < maxChartNotes; i++ ) chart[i].used = false;
  level = 1;
  applyLevel();
  scaleIdx = random(numScales);
  degree = 5;
  nextStepAt = 2000;                    // lead-in
  score = 0;
  combo = bestCombo = 0;
  energy = 100;
  notesPassed = 0;
  perfects = goods = misses = 0;
  judgement = "";
  judgedAt = 0;
  ringFlashUntil = 0;
}

// write the chart ahead so notes can fly in before they are due
static void generateAhead(unsigned long now){
  unsigned long stepMs = 30000UL / bpm;           // eighth notes
  while( nextStepAt < now + travelMs + 200 ){
    unsigned long t = nextStepAt;
    nextStepAt += stepMs;
    // more notes on the beat, density rises with the level
    bool onBeat = ((t / stepMs) % 2) == 0;
    int chance = onBeat ? min(95, 70 + level * 5) : min(60, 10 + level * 8);
    if( random(100) >= chance ) continue;

    int prev = degree;
    int move = random(100);
    if( move < 30 ) degree += 1;
    else if( move < 60 ) degree -= 1;
    else if( move < 72 ) degree += 0;
    else if( move < 86 ) degree += 2 + random(2);
    else degree -= 2 + random(2);
    degree = constrain(degree, 0, 9);            // two octaves
    int delta = degree - prev;
    uint8_t lane = delta == 0 ? LANE_X : delta == 1 ? LANE_UP : delta == -1 ? LANE_DOWN
                 : delta > 0 ? LANE_RIGHT : LANE_LEFT;

    for( int i = 0; i < maxChartNotes; i++ ){
      if( !chart[i].used ){
        chart[i] = {true, false, t, lane, scaleNote(scales[scaleIdx], 60, degree)};
        break;
      }
    }
  }
}

static void judge(const char *text){
  judgement = text;
  judgedAt = gameMillis();
}

static void noteMissed(){
  misses++;
  combo = 0;
  energy -= 12;
  judge("MISS");
  playSfx(sfxMissNote);
  rgbLed(1, true);
}

static bool lanePressed(const Buttons &p, int lane){
  switch( lane ){
    case LANE_UP: return p.up;
    case LANE_RIGHT: return p.right;
    case LANE_DOWN: return p.down;
    case LANE_LEFT: return p.left;
    default: return p.x;
  }
}

static bool pulseUpdate(const Buttons &held, const Buttons &pressed){
  unsigned long now = gameMillis();
  generateAhead(now);

  // taps
  for( int lane = 0; lane < 5; lane++ ){
    if( !lanePressed(pressed, lane) ) continue;
    int best = -1;
    long bestDt = 1000000;
    for( int i = 0; i < maxChartNotes; i++ ){
      const ChartNote &n = chart[i];
      if( !n.used || n.done || n.lane != lane ) continue;
      long dt = labs((long)now - (long)n.t);
      if( dt < bestDt ){ bestDt = dt; best = i; }
    }
    if( best >= 0 && bestDt <= (long)goodMs ){
      ChartNote &n = chart[best];
      n.done = true;
      bool perfect = bestDt <= (long)perfectMs;
      combo++;
      bestCombo = max(bestCombo, combo);
      score += (perfect ? 100 : 50) * (10 + min(combo, 50)) / 10;
      energy = min(100.0f, energy + (perfect ? 3.0f : 1.0f));
      if( perfect ){ perfects++; judge("PERFECT"); } else { goods++; judge("GOOD"); }
      hitSfx[0] = (uint16_t)midiToFreq(n.midi);
      hitSfx[1] = 150;
      hitSfx[2] = 0;
      hitSfx[3] = 0;
      playSfx(hitSfx);
      ringFlashUntil = now + 90;
      rgbLed(3, true);
    } else if( best < 0 || bestDt > (long)strayMs ){
      combo = 0;                       // stray tap
      energy -= 4;
      judge("OOPS");
    }
  }

  // notes that went past the ring
  for( int i = 0; i < maxChartNotes; i++ ){
    ChartNote &n = chart[i];
    if( !n.used ) continue;
    if( !n.done && now > n.t + goodMs ){
      n.done = true;
      noteMissed();
    }
    if( n.done && now > n.t + goodMs ){
      n.used = false;
      notesPassed++;
      if( notesPassed % notesPerLevel == 0 ){
        level++;
        applyLevel();
        scaleIdx = random(numScales);  // new key for the new level
        playSfx(sfxLevel);
      }
    }
  }

  if( now > ringFlashUntil + 60 ) rgbLed(3, false);
  if( now > judgedAt + 120 ) rgbLed(1, false);
  return energy > 0;
}

static void drawArrow(int x, int y, int lane){
  switch( lane ){
    case LANE_UP:    display.fillTriangle(x - 4, y - 3, x + 4, y - 3, x, y + 3, SSD1306_WHITE); break;
    case LANE_DOWN:  display.fillTriangle(x - 4, y + 3, x + 4, y + 3, x, y - 3, SSD1306_WHITE); break;
    case LANE_LEFT:  display.fillTriangle(x - 3, y - 4, x - 3, y + 4, x + 3, y, SSD1306_WHITE); break;
    case LANE_RIGHT: display.fillTriangle(x + 3, y - 4, x + 3, y + 4, x - 3, y, SSD1306_WHITE); break;
  }
}

static void pulseDraw(){
  unsigned long now = gameMillis();
  display.clearDisplay();
  display.setTextSize(1);

  // HUD
  display.setCursor(0, 0);
  display.print(score);
  if( combo >= 2 ){
    char c[8];
    snprintf(c, sizeof(c), "x%d", combo);
    display.setCursor(64 - strlen(c) * 3, 0);
    display.print(c);
  }
  display.drawRect(96, 1, 31, 6, SSD1306_WHITE);
  display.fillRect(97, 2, (int)(max(0.0f, energy) * 29 / 100), 4, SSD1306_WHITE);

  // guide lines along the four arms
  for( int d = pcRing + 3; d < 62; d += 4 ){
    if( pcCenterY - d >= 10 ) display.drawPixel(pcCenterX, pcCenterY - d, SSD1306_WHITE);
    if( pcCenterY + d <= 63 ) display.drawPixel(pcCenterX, pcCenterY + d, SSD1306_WHITE);
    display.drawPixel(pcCenterX - d, pcCenterY, SSD1306_WHITE);
    display.drawPixel(pcCenterX + d, pcCenterY, SSD1306_WHITE);
  }

  // target ring
  if( now < ringFlashUntil ){
    display.fillCircle(pcCenterX, pcCenterY, pcRing, SSD1306_WHITE);
  } else {
    display.drawCircle(pcCenterX, pcCenterY, pcRing, SSD1306_WHITE);
    display.drawPixel(pcCenterX, pcCenterY, SSD1306_WHITE);
  }

  // notes: f = 0 when they appear at the edge, 1 when they reach the ring
  for( int i = 0; i < maxChartNotes; i++ ){
    const ChartNote &n = chart[i];
    if( !n.used || n.done ) continue;
    float f = 1.0f - ((long)n.t - (long)now) / (float)travelMs;
    if( f < 0 ) continue;
    switch( n.lane ){
      case LANE_UP:    drawArrow(pcCenterX, (int)(11 + (pcCenterY - pcRing - 11) * f), n.lane); break;
      case LANE_DOWN:  drawArrow(pcCenterX, (int)(62 - (62 - pcCenterY - pcRing) * f), n.lane); break;
      case LANE_LEFT:  drawArrow((int)(3 + (pcCenterX - pcRing - 3) * f), pcCenterY, n.lane); break;
      case LANE_RIGHT: drawArrow((int)(124 - (124 - pcCenterX - pcRing) * f), pcCenterY, n.lane); break;
      case LANE_X: {
        int r = (int)(26 - (26 - pcRing) * f);
        if( r >= pcRing ) display.drawCircle(pcCenterX, pcCenterY, r, SSD1306_WHITE);
        break;
      }
    }
  }

  // judgement and level
  if( now - judgedAt < 500 ){
    display.setCursor(0, 56);
    display.print(judgement);
  }
  display.setCursor(98, 56);
  display.printf("L%d", level);
  display.setCursor(98, 12);
  display.print(scales[scaleIdx].name);
}

static uint32_t pulseScore(){
  return score;
}

static void pulseSummary(char *buf, size_t len){
  snprintf(buf, len, "L%d combo %d P%d G%d", level, bestCombo, perfects, goods);
}

extern const Game pulseGame = {
  "PULSE CROSS", "pulse", "hit notes, play music",
  pulseStart, pulseUpdate, pulseDraw, pulseScore, pulseSummary, nullptr, false, false, true,
};
