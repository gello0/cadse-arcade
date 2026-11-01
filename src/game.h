/* Interface between the arcade menu and the individual games
 *
 * Each game lives in src/games/ and exports one Game struct. The menu
 * handles pausing, game over, high scores and the SD card, so a game only
 * has to start, update one frame, and draw one frame.
 */
#pragma once

#include <Arduino.h>
#include "hw.h"

const int screenW = 128;
const int screenH = 64;

struct Game {
  const char *title;
  const char *id;                                  // file name on the SD card
  const char *help;                                // one line shown in the menu
  void (*start)();
  bool (*update)(const Buttons &held, const Buttons &pressed);  // false = game over
  void (*draw)();                                  // draw into the buffer, don't call display()
  uint32_t (*score)();
  void (*summary)(char *buf, size_t len);          // extra line on the game over screen
  void (*resume)();                                // after a pause, may be nullptr
  bool versus;                                     // two players: no high scores, summary names the winner
  bool online;                                     // played over the radio: X doesn't pause
  bool xIsInput;                                   // X is a game control: both bumpers pause instead
};

extern const Game orbitGame;
extern const Game cometGame;
extern const Game breakerGame;
extern const Game marbleGame;
extern const Game caveGame;
extern const Game tankGame;
extern const Game patrolGame;
extern const Game cargoGame;
extern const Game duelGame;
extern const Game pulseGame;

// game clock in ms that stands still while paused
void gameClockReset();
void gameClockPause();
void gameClockResume();
unsigned long gameMillis();

// shared drawing helpers
void centerText(const char *text, int y, int size = 1);
void initStars();
void updateStars(float speed, int top = 0);
void drawStars();
void drawBatteryIcon(int x, int y, uint16_t color);  // 20x7 px incl. charging bolt

// shared sound effects
extern const uint16_t sfxClick[];
extern const uint16_t sfxBlip[];
extern const uint16_t sfxPickup[];
extern const uint16_t sfxBoom[];
extern const uint16_t sfxHit[];
extern const uint16_t sfxLevel[];
extern const uint16_t sfxStart[];
extern const uint16_t sfxGameOver[];
