/* TILT SETUP - works out how the IMU is mounted on this board
 *
 * 1. hold the board flat           -> baseline
 * 2. tilt it to the right          -> the axis that changes most is left/right
 * 3. tip the screen's bottom down  -> the sign of the other axis is up/down
 * 4. test with a dot, X saves to flash, touch Left starts over
 * Each tilt step is detected automatically once it is held for half a second.
 */
#include "calibrate.h"
#include "game.h"

const float calibThreshold = 3.5f;        // m/s^2 change that counts as tilted
const unsigned long calibHoldMs = 500;

enum CalibStep { CAL_FLAT, CAL_RIGHT, CAL_DOWN, CAL_TEST };

static CalibStep step;
static float baseA, baseB;
static unsigned long tiltedSince;
static int newAxis, newXSign, newYSign;
static int oldAxis, oldXSign, oldYSign;
static float dotX, dotY;

void calibStart(){
  step = CAL_FLAT;
  tiltedSince = 0;
  oldAxis = tiltXAxis;
  oldXSign = tiltXSign;
  oldYSign = tiltYSign;
}

// true once the board has been tilted past the threshold for long enough
static bool heldTilted(float da, float db){
  bool tilted = max(fabsf(da), fabsf(db)) > calibThreshold;
  if( !tilted ){
    tiltedSince = 0;
    return false;
  }
  if( tiltedSince == 0 ){
    tiltedSince = millis();
  }
  return millis() - tiltedSince > calibHoldMs;
}

bool calibUpdate(const Buttons &held, const Buttons &pressed){
  if( !imuAvailable() ){
    return !(pressed.x || pressed.left);
  }
  float a, b;
  readAccelRaw(a, b);
  float da = a - baseA, db = b - baseB;

  switch( step ){
    case CAL_FLAT:
      if( pressed.x ){
        baseA = a;
        baseB = b;
        tiltedSince = 0;
        step = CAL_RIGHT;
        playSfx(sfxClick);
      }
      if( pressed.left ){
        return false;
      }
      break;

    case CAL_RIGHT:
      if( heldTilted(da, db) ){
        newAxis = fabsf(da) > fabsf(db) ? 0 : 1;
        float d = newAxis == 0 ? da : db;
        newXSign = d > 0 ? 1 : -1;
        tiltedSince = 0;
        step = CAL_DOWN;
        playSfx(sfxPickup);
      }
      break;

    case CAL_DOWN: {
      float d = newAxis == 0 ? db : da;   // the other axis
      if( fabsf(d) > calibThreshold ){
        if( heldTilted(0, d) ){
          newYSign = d > 0 ? 1 : -1;
          // apply now so the test dot uses the new mapping
          tiltXAxis = newAxis;
          tiltXSign = newXSign;
          tiltYSign = newYSign;
          dotX = screenW / 2;
          dotY = 36;
          step = CAL_TEST;
          playSfx(sfxPickup);
        }
      } else {
        tiltedSince = 0;
      }
      break;
    }

    case CAL_TEST: {
      float x, y;
      readTilt2(x, y);
      dotX = constrain(dotX + x * 0.4f, 3.0f, (float)screenW - 4);
      dotY = constrain(dotY + y * 0.4f, 14.0f, 50.0f);
      if( pressed.x ){
        setTiltConfig(newAxis, newXSign, newYSign);
        playSfx(sfxLevel);
        return false;
      }
      if( pressed.left ){
        tiltXAxis = oldAxis;      // undo until saved
        tiltXSign = oldXSign;
        tiltYSign = oldYSign;
        step = CAL_FLAT;
        playSfx(sfxClick);
      }
      break;
    }
  }
  return true;
}

void calibDraw(){
  display.clearDisplay();
  display.fillRect(0, 0, screenW, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print("TILT SETUP");
  display.setTextColor(SSD1306_WHITE);

  if( !imuAvailable() ){
    centerText("IMU not found", 26);
    centerText("touch X", 54);
    display.display();
    return;
  }

  // progress bar while a tilt is being held
  if( tiltedSince && (step == CAL_RIGHT || step == CAL_DOWN) ){
    int w = min(100UL, (millis() - tiltedSince) * 100 / calibHoldMs);
    display.drawRect(14, 46, 100, 5, SSD1306_WHITE);
    display.fillRect(14, 46, w, 5, SSD1306_WHITE);
  }

  switch( step ){
    case CAL_FLAT:
      centerText("1/3  lay the board", 16);
      centerText("flat, screen up", 26);
      centerText("X go  < back", 55);
      break;
    case CAL_RIGHT:
      centerText("2/3  tilt the board", 16);
      centerText("to the RIGHT  >>>", 28);
      centerText("and hold it", 55);
      break;
    case CAL_DOWN:
      centerText("3/3  tip the BOTTOM", 16);
      centerText("edge of the screen", 26);
      centerText("down, toward you", 36);
      centerText("and hold it", 55);
      break;
    case CAL_TEST:
      display.drawRect(0, 11, screenW, 42, SSD1306_WHITE);
      display.fillCircle((int)dotX, (int)dotY, 2, SSD1306_WHITE);
      centerText("X save  < redo", 55);
      break;
  }
  display.display();
}
