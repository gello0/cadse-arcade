/* DEBRIS BREAKER
 *
 * Bounce a probe off your deflector to smash a wall of space debris.
 *   Tilt / touch Left+Right : move the deflector
 *   Either bumper / touch Up: launch the probe
 * Where the probe hits the deflector sets its angle. Doubled blocks take two
 * hits, and some blocks drop a capsule that widens the deflector. Each
 * cleared wall is faster than the last.
 */
#include "../game.h"

const int brkHudH = 9;
const int brkRows = 5;
const int brkCols = 8;
const int brickW = 16;
const int brickH = 5;
const int brickTop = 12;
const int paddleY = 60;
const int paddleNormalW = 20;
const int paddleWideW = 32;
const unsigned long wideDurationMs = 10000;

const float brkTiltGain = 1.1f;
const float brkTouchSpeed = 3.0f;

static uint8_t bricks[brkRows][brkCols];   // remaining hits, 0 = gone
static float paddleX;
static int paddleW;
static unsigned long wideUntil;
static float ballX, ballY, ballVX, ballVY;
static bool ballStuck;
static float speed;
static int balls;
static int wall;
static uint32_t score;
static int bricksBroken;
static float tiltZero;

static bool capsuleActive;
static float capsuleX, capsuleY;

static void buildWall(){
  for( int r = 0; r < brkRows; r++ ){
    for( int c = 0; c < brkCols; c++ ){
      uint8_t hp = 1;
      switch( wall % 4 ){
        case 1: hp = (r < 2) ? 2 : 1; break;                     // armoured top
        case 2: hp = ((r + c) % 2) ? 1 : 0; break;               // checkerboard
        case 3: hp = (c == 0 || c == brkCols - 1 || r == 0) ? 2 : 1; break;  // frame
      }
      if( wall >= 4 && random(4) == 0 ){
        hp = 2;
      }
      bricks[r][c] = hp;
    }
  }
}

static void stickBall(){
  ballStuck = true;
  ballX = paddleX;
  ballY = paddleY - 2;
}

static void breakerStart(){
  wall = 0;
  score = 0;
  balls = 3;
  bricksBroken = 0;
  speed = 1.8f;
  paddleX = screenW / 2;
  paddleW = paddleNormalW;
  wideUntil = 0;
  capsuleActive = false;
  tiltZero = readTilt();
  buildWall();
  stickBall();
}

static void breakerResume(){
  tiltZero = readTilt();
}

static bool wallCleared(){
  for( int r = 0; r < brkRows; r++ ){
    for( int c = 0; c < brkCols; c++ ){
      if( bricks[r][c] ) return false;
    }
  }
  return true;
}

static void hitBrick(int r, int c){
  bricks[r][c]--;
  if( bricks[r][c] == 0 ){
    bricksBroken++;
    score += 10 + wall * 2;
    playSfx(sfxBoom);
    if( !capsuleActive && random(8) == 0 ){
      capsuleActive = true;
      capsuleX = c * brickW + brickW / 2;
      capsuleY = brickTop + r * brickH;
    }
  } else {
    score += 2;
    playSfx(sfxBlip);
  }
}

static bool breakerUpdate(const Buttons &held, const Buttons &pressed){
  unsigned long now = gameMillis();

  // deflector
  if( now > wideUntil ){
    paddleW = paddleNormalW;
  }
  float v = (readTilt() - tiltZero) * brkTiltGain;
  if( held.left ) v -= brkTouchSpeed;
  if( held.right ) v += brkTouchSpeed;
  paddleX = constrain(paddleX + v, (float)paddleW / 2, (float)(screenW - paddleW / 2));

  if( ballStuck ){
    ballX = paddleX;
    if( pressed.fire || pressed.shield || pressed.up ){
      ballStuck = false;
      ballVX = speed * (random(2) ? 0.5f : -0.5f);
      ballVY = -sqrtf(speed * speed - ballVX * ballVX);
      playSfx(sfxBlip);
    }
  } else {
    // move in substeps so the probe can't tunnel through bricks
    const int steps = 3;
    for( int s = 0; s < steps; s++ ){
      float prevX = ballX, prevY = ballY;
      ballX += ballVX / steps;
      ballY += ballVY / steps;

      if( ballX < 1 ){ ballX = 1; ballVX = fabsf(ballVX); playSfx(sfxClick); }
      if( ballX > screenW - 2 ){ ballX = screenW - 2; ballVX = -fabsf(ballVX); playSfx(sfxClick); }
      if( ballY < brkHudH + 1 ){ ballY = brkHudH + 1; ballVY = fabsf(ballVY); playSfx(sfxClick); }

      // deflector: hit position sets the angle
      if( ballVY > 0 && ballY + 1 >= paddleY && ballY <= paddleY + 2 &&
          fabsf(ballX - paddleX) <= paddleW / 2 + 1 ){
        float offset = constrain((ballX - paddleX) / (paddleW / 2.0f), -1.0f, 1.0f);
        ballVX = speed * offset * 0.85f;
        ballVY = -sqrtf(speed * speed - ballVX * ballVX);
        ballY = paddleY - 2;
        playSfx(sfxBlip);
      }

      // bricks: flip the axis along which we entered the brick
      int c = (int)ballX / brickW;
      int r = ((int)ballY - brickTop) / brickH;
      if( ballY >= brickTop && r < brkRows && c >= 0 && c < brkCols && bricks[r][c] ){
        int prevC = (int)prevX / brickW;
        int prevR = ((int)prevY - brickTop) / brickH;
        if( prevR != r || prevY < brickTop ){
          ballVY = -ballVY;
        } else if( prevC != c ){
          ballVX = -ballVX;
        } else {
          ballVY = -ballVY;
        }
        ballX = prevX;
        ballY = prevY;
        hitBrick(r, c);
        break;
      }
    }

    if( ballY > screenH ){
      balls--;
      playSfx(sfxHit);
      rgbLed(1, true);
      if( balls <= 0 ){
        return false;
      }
      stickBall();
    }
  }

  if( capsuleActive ){
    capsuleY += 0.8f;
    if( capsuleY >= paddleY - 2 && capsuleY <= paddleY + 3 && fabsf(capsuleX - paddleX) <= paddleW / 2 + 2 ){
      capsuleActive = false;
      paddleW = paddleWideW;
      wideUntil = now + wideDurationMs;
      score += 15;
      playSfx(sfxPickup);
    } else if( capsuleY > screenH ){
      capsuleActive = false;
    }
  }

  if( wallCleared() ){
    wall++;
    score += 100;
    speed = min(3.4f, speed + 0.25f);
    buildWall();
    stickBall();
    capsuleActive = false;
    playSfx(sfxLevel);
  }

  rgbLed(1, ballStuck && balls < 3 && (now / 200) % 2);
  rgbLed(3, paddleW == paddleWideW);
  return true;
}

static void breakerDraw(){
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(score);
  display.setCursor(56, 0);
  display.printf("W%d", wall + 1);
  for( int i = 0; i < balls; i++ ){
    display.fillCircle(124 - i * 6, 3, 2, SSD1306_WHITE);
  }
  display.drawFastHLine(0, brkHudH - 1, screenW, SSD1306_WHITE);

  for( int r = 0; r < brkRows; r++ ){
    for( int c = 0; c < brkCols; c++ ){
      int x = c * brickW;
      int y = brickTop + r * brickH;
      if( bricks[r][c] == 1 ){
        display.fillRect(x + 1, y, brickW - 2, brickH - 1, SSD1306_WHITE);
      } else if( bricks[r][c] >= 2 ){
        display.drawRect(x + 1, y, brickW - 2, brickH - 1, SSD1306_WHITE);
        display.drawFastHLine(x + 3, y + 2, brickW - 6, SSD1306_WHITE);
      }
    }
  }

  if( capsuleActive ){
    display.drawRoundRect((int)capsuleX - 3, (int)capsuleY - 2, 7, 4, 2, SSD1306_WHITE);
  }

  display.fillRoundRect((int)paddleX - paddleW / 2, paddleY, paddleW, 3, 1, SSD1306_WHITE);
  display.fillRect((int)ballX - 1, (int)ballY - 1, 3, 3, SSD1306_WHITE);

  if( ballStuck && (millis() / 400) % 2 ){
    centerText("bumper to launch", 44);
  }
}

static uint32_t breakerScore(){
  return score;
}

static void breakerSummary(char *buf, size_t len){
  snprintf(buf, len, "wall %d blocks %d", wall + 1, bricksBroken);
}

extern const Game breakerGame = {
  "DEBRIS BREAKER", "breaker", "tilt the deflector",
  breakerStart, breakerUpdate, breakerDraw, breakerScore, breakerSummary, breakerResume,
};
