/* SYNTH
 *
 *   Touch Left, Down, X, Up, Right   the 5 notes of the scale, low to high
 *   Tilt left / right                 pitch bend, up to 2 semitones
 *   Tilt forward / back               octave down / up
 *   Left bumper                       next preset (sound + scale)
 *   Right bumper                      play a demo song, again for the next
 *                                     one, after the last it stops
 *   Both bumpers                      back to the menu
 *
 * The buzzer is a single square-wave voice, so the sounds come from pulse
 * width (timbre), pitch modulation and fast note switching. Demo songs are
 * public-domain classics plus original chiptune tracks; they play through
 * the current preset (chiptunes pick a fitting one at the start), so tilt
 * and preset changes work on them too. Touching a pad stops the song.
 */
#include "synth.h"
#include "game.h"
#include "music.h"

const int synthRoot = 60;                 // C4
const float bendRange = 2.0f;             // semitones at full tilt
const float bendDeadzone = 1.2f;          // m/s^2
const float octaveTilt = 4.0f;            // m/s^2 forward/back for an octave shift

enum SynthSound { SND_SQUARE, SND_PULSE, SND_VIBRATO, SND_ARP, SND_SLIDE, SND_LASER };

struct Preset {
  const char *name;
  uint8_t sound;
  uint8_t scale;          // index into scales[] in music.h
};

static const Preset presets[] = {
  {"CHIPTUNE", SND_SQUARE, 0},
  {"8-BIT LEAD", SND_PULSE, 0},
  {"MOODY", SND_PULSE, 1},
  {"SPACE PAD", SND_VIBRATO, 3},
  {"DESERT", SND_VIBRATO, 4},
  {"BLUES SLIDE", SND_SLIDE, 2},
  {"ARP MAJOR", SND_ARP, 0},
  {"ARP MINOR", SND_ARP, 1},
  {"KOTO", SND_PULSE, 3},
  {"LASER GUN", SND_LASER, 1},
};
static const int numPresets = sizeof(presets) / sizeof(presets[0]);

// ---------------------------------------------------------------- demo songs
// pairs of {midi note, length in eighths}, 0 = rest, ended by {255, 0}

static const uint8_t odeToJoy[] = {
  64,2, 64,2, 65,2, 67,2, 67,2, 65,2, 64,2, 62,2, 60,2, 60,2, 62,2, 64,2, 64,3, 62,1, 62,4,
  64,2, 64,2, 65,2, 67,2, 67,2, 65,2, 64,2, 62,2, 60,2, 60,2, 62,2, 64,2, 62,3, 60,1, 60,4,
  255,0,
};

static const uint8_t furElise[] = {
  76,1, 75,1, 76,1, 75,1, 76,1, 71,1, 74,1, 72,1, 69,3, 0,1,
  60,1, 64,1, 69,1, 71,3, 0,1, 64,1, 68,1, 71,1, 72,3, 0,1,
  64,1, 76,1, 75,1, 76,1, 75,1, 76,1, 71,1, 74,1, 72,1, 69,3, 0,1,
  60,1, 64,1, 69,1, 71,3, 0,1, 64,1, 72,1, 71,1, 69,4,
  255,0,
};

static const uint8_t mountainKing[] = {
  57,1, 59,1, 60,1, 62,1, 64,1, 60,1, 64,2, 63,1, 59,1, 63,2, 62,1, 58,1, 62,2,
  57,1, 59,1, 60,1, 62,1, 64,1, 60,1, 64,1, 69,1, 67,1, 64,1, 60,1, 64,1, 67,4,
  255,0,
};

static const uint8_t twinkle[] = {
  60,2, 60,2, 67,2, 67,2, 69,2, 69,2, 67,4, 65,2, 65,2, 64,2, 64,2, 62,2, 62,2, 60,4,
  67,2, 67,2, 65,2, 65,2, 64,2, 64,2, 62,4, 67,2, 67,2, 65,2, 65,2, 64,2, 64,2, 62,4,
  60,2, 60,2, 67,2, 67,2, 69,2, 69,2, 67,4, 65,2, 65,2, 64,2, 64,2, 62,2, 62,2, 60,4,
  255,0,
};

static const uint8_t frereJacques[] = {
  60,2, 62,2, 64,2, 60,2, 60,2, 62,2, 64,2, 60,2,
  64,2, 65,2, 67,4, 64,2, 65,2, 67,4,
  67,1, 69,1, 67,1, 65,1, 64,2, 60,2, 67,1, 69,1, 67,1, 65,1, 64,2, 60,2,
  60,2, 55,2, 60,4, 60,2, 55,2, 60,4,
  255,0,
};

// original chiptune tracks written for the CADSE board; here one length unit
// is a sixteenth at their faster tempo

static const uint8_t starhop[] = {
  69,1, 72,1, 76,1, 81,1, 79,1, 76,1, 72,1, 76,1,
  77,1, 81,1, 84,1, 81,1, 79,1, 76,1, 79,1, 83,1,
  81,2, 79,1, 76,1, 74,2, 72,2,
  71,1, 74,1, 79,1, 74,1, 76,4,
  69,1, 72,1, 76,1, 81,1, 79,1, 76,1, 72,1, 76,1,
  77,1, 81,1, 84,1, 81,1, 83,1, 84,1, 86,1, 88,1,
  77,1, 76,1, 74,1, 72,1, 71,2, 74,2,
  69,4, 0,2, 81,1, 81,1,
  255,0,
};

static const uint8_t pixelRun[] = {
  72,1, 76,1, 79,1, 84,1, 79,1, 76,1, 72,2,
  74,1, 77,1, 81,1, 86,1, 81,1, 77,1, 74,2,
  76,1, 79,1, 83,1, 88,1, 83,1, 79,1, 76,2,
  77,2, 76,1, 74,1, 72,4,
  79,1, 79,1, 0,1, 79,1, 81,2, 79,2,
  77,1, 76,1, 74,1, 76,1, 72,4,
  79,1, 79,1, 0,1, 79,1, 81,2, 83,2,
  84,2, 79,1, 76,1, 72,4,
  255,0,
};

static const uint8_t nebula[] = {
  74,2, 77,2, 81,4,
  79,2, 77,2, 76,4,
  74,2, 76,2, 77,2, 79,2,
  81,6, 0,2,
  82,2, 81,2, 79,4,
  77,2, 76,2, 74,4,
  73,2, 76,2, 79,2, 76,2,
  74,8,
  255,0,
};

static const uint8_t bossBattle[] = {
  52,1, 52,1, 64,1, 52,1, 52,1, 62,1, 52,1, 60,1,
  52,1, 52,1, 59,1, 52,1, 60,1, 59,1, 55,1, 57,1,
  52,1, 52,1, 64,1, 52,1, 52,1, 62,1, 52,1, 60,1,
  52,1, 52,1, 59,1, 52,1, 55,1, 57,1, 59,1, 62,1,
  64,2, 67,2, 71,2, 70,1, 69,1,
  67,2, 64,2, 62,4,
  64,2, 67,2, 71,2, 74,1, 72,1,
  71,2, 70,2, 64,4,
  255,0,
};

struct Song {
  const char *name;
  uint16_t eighthMs;      // length of one unit
  const uint8_t *notes;
  int8_t preset;          // preset to switch to when the song starts, -1 = keep
};

static const Song songs[] = {
  {"ODE TO JOY", 250, odeToJoy, -1},
  {"FUR ELISE", 180, furElise, -1},
  {"MOUNTAIN KING", 170, mountainKing, -1},
  {"TWINKLE", 230, twinkle, -1},
  {"FRERE JACQUES", 220, frereJacques, -1},
  {"STARHOP", 115, starhop, 1},         // 8-BIT LEAD
  {"PIXEL RUN", 120, pixelRun, 0},      // CHIPTUNE
  {"NEBULA", 210, nebula, 3},           // SPACE PAD
  {"BOSS BATTLE", 105, bossBattle, 2},  // MOODY
};
static const int numSongs = sizeof(songs) / sizeof(songs[0]);

// ---------------------------------------------------------------- state

static int preset;
static int activeKey;                     // -1 = silent, else 0..4
static unsigned long keySince;            // start of the current note, for the effects
static float zeroX, zeroY;
static float bend;                        // semitones
static int octave;
static float slideMidi;                   // current pitch while sliding
static float outMidi;                     // what is sounding, for the display
static int outWidth;
static bool exitArmed;

static int song;                          // -1 = no song
static int songPos;                       // index into the note pairs
static unsigned long noteStart, noteEnd;
static int songMidi;                      // 0 while resting

// pads in order of pitch, low to high
static bool keyHeld(const Buttons &b, int k){
  switch( k ){
    case 0: return b.left;
    case 1: return b.down;
    case 2: return b.x;
    case 3: return b.up;
    default: return b.right;
  }
}

static void startSong(int s){
  song = s;
  if( songs[s].preset >= 0 ){
    preset = songs[s].preset;
  }
  songPos = 0;
  noteStart = millis();
  noteEnd = noteStart;      // the first note starts on the next update
  songMidi = 0;
}

static void stopSong(){
  song = -1;
  songMidi = 0;
}

// advance the song; plays each note for 85 % of its length so notes separate
static void updateSong(){
  if( song < 0 ) return;
  unsigned long now = millis();
  const Song &s = songs[song];
  if( now >= noteEnd ){
    uint8_t midi = s.notes[songPos * 2];
    if( midi == 255 ){
      stopSong();
      return;
    }
    uint8_t len = s.notes[songPos * 2 + 1];
    songMidi = midi;
    noteStart = now;
    noteEnd = now + (unsigned long)len * s.eighthMs;
    keySince = now;
    songPos++;
  }
  if( now > noteStart + (noteEnd - noteStart) * 85 / 100 ){
    songMidi = 0;
  }
}

void synthStart(){
  preset = 0;
  activeKey = -1;
  bend = 0;
  octave = 0;
  slideMidi = synthRoot;
  outMidi = 0;
  outWidth = 4;
  exitArmed = false;
  stopSong();
  readTilt2(zeroX, zeroY);
  toneOut(0);
}

bool synthUpdate(const Buttons &held, const Buttons &pressed){
  // both bumpers: leave
  if( held.fire && held.shield ){
    exitArmed = true;
  }
  if( exitArmed ){
    toneOut(0);
    stopSong();
    if( !held.fire && !held.shield ){
      rgbLed(1, false);
      rgbLed(3, false);
      return false;
    }
    return true;
  }
  if( pressed.fire ) preset = (preset + 1) % numPresets;
  if( pressed.shield ){
    if( song < numSongs - 1 ) startSong(song + 1);
    else stopSong();
  }

  // the most recently pressed key that is still held plays
  for( int k = 0; k < 5; k++ ){
    if( keyHeld(pressed, k) ){
      activeKey = k;
      keySince = millis();
      stopSong();            // playing yourself takes over from the demo
    }
  }
  if( activeKey >= 0 && !keyHeld(held, activeKey) ){
    activeKey = -1;
    for( int k = 4; k >= 0; k-- ){
      if( keyHeld(held, k) ){ activeKey = k; break; }
    }
  }
  updateSong();

  // tilt: left/right bends, forward/back shifts the octave
  float tx, ty;
  readTilt2(tx, ty);
  tx -= zeroX;
  ty -= zeroY;
  float bx = fabsf(tx) < bendDeadzone ? 0 : (tx - (tx > 0 ? bendDeadzone : -bendDeadzone));
  bend = constrain(bx / 6.0f * bendRange, -bendRange, bendRange);
  octave = ty > octaveTilt ? -1 : ty < -octaveTilt ? 1 : 0;

  const Preset &p = presets[preset];
  float midi;
  if( activeKey >= 0 ){
    midi = scaleNote(scales[p.scale], synthRoot + octave * 12, activeKey);
  } else if( songMidi > 0 ){
    midi = songMidi + octave * 12;
  } else {
    toneOut(0);
    outMidi = 0;
    rgbLed(1, false);
    rgbLed(3, false);
    return true;
  }
  midi += bend;

  float t = (millis() - keySince) / 1000.0f;
  int width = 4;
  switch( p.sound ){
    case SND_PULSE:
      width = 1;
      break;
    case SND_VIBRATO:
      width = 2;
      midi += 0.35f * sinf(2 * PI * 6.0f * t) * min(1.0f, t * 3);   // vibrato fades in
      break;
    case SND_ARP: {
      static const int8_t arpMajor[4] = {0, 4, 7, 12}, arpMinor[4] = {0, 3, 7, 12};
      const int8_t *arp = (p.scale == 1) ? arpMinor : arpMajor;
      midi += arp[(int)(t * 14) % 4];
      width = 2;
      break;
    }
    case SND_SLIDE:
      slideMidi += (midi - slideMidi) * 0.18f;   // glide towards the new note
      midi = slideMidi;
      width = 3;
      break;
    case SND_LASER:
      midi += 24.0f * expf(-t * 18.0f);           // zap down from two octaves up
      width = 1;
      break;
  }
  if( p.sound != SND_SLIDE ){
    slideMidi = midi;
  }

  toneOut((uint32_t)midiToFreq(midi), width);
  outMidi = midi;
  outWidth = width;
  int lit = activeKey >= 0 ? activeKey : (int)(midi) % 5;
  rgbLed(1, lit <= 1);
  rgbLed(3, lit >= 3);
  return true;
}

void synthDraw(){
  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0, 0, screenW, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(presets[preset].name);
  const char *sc = scales[presets[preset].scale].name;
  display.setCursor(screenW - 2 - strlen(sc) * 6, 1);
  display.print(sc);
  display.setTextColor(SSD1306_WHITE);

  // note name and frequency
  if( outMidi > 0 ){
    char name[8];
    noteName(lroundf(outMidi), name, sizeof(name));
    display.setTextSize(2);
    display.setCursor(2, 14);
    display.print(name);
    display.setTextSize(1);
    display.setCursor(2, 32);
    display.printf("%d Hz", (int)midiToFreq(outMidi));
  } else if( song < 0 ){
    display.setCursor(2, 18);
    display.print("touch a pad");
  }

  // waveform: one pulse train, period follows the pitch
  if( outMidi > 0 ){
    int period = constrain((int)(2600.0f / midiToFreq(outMidi) * 10), 6, 40);
    int high = max(1, period * outWidth / 8);
    int y0 = 30, y1 = 16;
    int x = 62;
    while( x < screenW - 2 ){
      int xe = min(x + high, screenW - 2);
      display.drawFastHLine(x, y1, xe - x, SSD1306_WHITE);
      display.drawFastVLine(xe, y1, y0 - y1, SSD1306_WHITE);
      int xl = min(xe + period - high, screenW - 2);
      display.drawFastHLine(xe, y0, xl - xe, SSD1306_WHITE);
      if( xl < screenW - 2 ) display.drawFastVLine(xl, y1, y0 - y1, SSD1306_WHITE);
      x = xl;
    }
  } else {
    display.drawFastHLine(62, 23, screenW - 64, SSD1306_WHITE);
  }

  // octave and bend
  display.setCursor(62, 33);
  display.printf("oct%+d bend%+.1f", octave, bend);

  if( song >= 0 ){
    // song title and progress instead of the keys
    display.setCursor(4, 45);
    display.printf("> %s", songs[song].name);
    int total = 0;
    while( songs[song].notes[total * 2] != 255 ) total++;
    display.drawRect(4, 55, 120, 6, SSD1306_WHITE);
    display.fillRect(5, 56, songPos * 118 / max(1, total), 4, SSD1306_WHITE);
  } else {
    // the five keys, lit while playing
    for( int k = 0; k < 5; k++ ){
      int x = 4 + k * 25;
      bool lit = k == activeKey;
      if( lit ) display.fillRect(x, 44, 21, 12, SSD1306_WHITE);
      else display.drawRect(x, 44, 21, 12, SSD1306_WHITE);
      static const char *labels[5] = {"L", "D", "X", "U", "R"};
      display.setTextColor(lit ? SSD1306_BLACK : SSD1306_WHITE);
      display.setCursor(x + 8, 46);
      display.print(labels[k]);
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(4, 57);
    display.print("< preset   song >");
  }
  display.display();
}
