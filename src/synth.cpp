/* SYNTH
 *
 *   Touch Left, Down, X, Up, Right   the 5 notes of the scale, low to high
 *   Tilt left / right                 pitch bend, up to 2 semitones
 *   Tilt forward / back               octave down / up
 *   Left bumper                       next sound
 *   Right bumper                      next scale
 *   Both bumpers                      back to the menu
 *
 * The buzzer is a single square-wave voice, so the sounds come from pulse
 * width (timbre), pitch modulation and fast note switching.
 */
#include "synth.h"
#include "game.h"
#include "music.h"

const int synthRoot = 60;                 // C4
const float bendRange = 2.0f;             // semitones at full tilt
const float bendDeadzone = 1.2f;          // m/s^2
const float octaveTilt = 4.0f;            // m/s^2 forward/back for an octave shift

enum SynthSound { SND_SQUARE, SND_PULSE, SND_VIBRATO, SND_ARP, SND_SLIDE, SND_LASER, NUM_SOUNDS };
static const char *soundNames[NUM_SOUNDS] = {"SQUARE", "PULSE", "VIBRATO", "ARP", "SLIDE", "LASER"};

static int sound;
static int scaleIdx;
static int activeKey;                     // -1 = silent, else 0..4
static unsigned long keySince;
static float zeroX, zeroY;
static float bend;                        // semitones
static int octave;
static float slideMidi;                   // current pitch while sliding
static float outMidi;                     // what is sounding, for the display
static int outWidth;
static bool exitArmed;

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

void synthStart(){
  sound = SND_SQUARE;
  scaleIdx = 0;
  activeKey = -1;
  bend = 0;
  octave = 0;
  slideMidi = synthRoot;
  outMidi = 0;
  outWidth = 4;
  exitArmed = false;
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
    if( !held.fire && !held.shield ){
      rgbLed(1, false);
      rgbLed(3, false);
      return false;
    }
    return true;
  }
  if( pressed.fire ) sound = (sound + 1) % NUM_SOUNDS;
  if( pressed.shield ) scaleIdx = (scaleIdx + 1) % numScales;

  // the most recently pressed key that is still held plays
  for( int k = 0; k < 5; k++ ){
    if( keyHeld(pressed, k) ){
      activeKey = k;
      keySince = millis();
    }
  }
  if( activeKey >= 0 && !keyHeld(held, activeKey) ){
    activeKey = -1;
    for( int k = 4; k >= 0; k-- ){
      if( keyHeld(held, k) ){ activeKey = k; break; }
    }
  }

  // tilt: left/right bends, forward/back shifts the octave
  float tx, ty;
  readTilt2(tx, ty);
  tx -= zeroX;
  ty -= zeroY;
  float bx = fabsf(tx) < bendDeadzone ? 0 : (tx - (tx > 0 ? bendDeadzone : -bendDeadzone));
  bend = constrain(bx / 6.0f * bendRange, -bendRange, bendRange);
  octave = ty > octaveTilt ? -1 : ty < -octaveTilt ? 1 : 0;

  if( activeKey < 0 ){
    toneOut(0);
    outMidi = 0;
    rgbLed(1, false);
    rgbLed(3, false);
    return true;
  }

  float midi = scaleNote(scales[scaleIdx], synthRoot + octave * 12, activeKey) + bend;
  float t = (millis() - keySince) / 1000.0f;
  int width = 4;
  switch( sound ){
    case SND_PULSE:
      width = 1;
      break;
    case SND_VIBRATO:
      width = 2;
      midi += 0.35f * sinf(2 * PI * 6.0f * t) * min(1.0f, t * 3);   // vibrato fades in
      break;
    case SND_ARP: {
      static const int8_t arp[4] = {0, 4, 7, 12};
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
  if( sound != SND_SLIDE ){
    slideMidi = midi;
  }

  toneOut((uint32_t)midiToFreq(midi), width);
  outMidi = midi;
  outWidth = width;
  rgbLed(1, activeKey <= 1);
  rgbLed(3, activeKey >= 3);
  return true;
}

void synthDraw(){
  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0, 0, screenW, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(soundNames[sound]);
  const char *sc = scales[scaleIdx].name;
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
  } else {
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
  display.print("bumpers: sound/scale");
  display.display();
}
