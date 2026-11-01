/* CADSE ARCADE - a collection of games for the CADSE PCB
 *
 * Menu:    touch Up/Down to choose, X or left bumper to play,
 *          touch Right to see that game's high scores,
 *          after the games: SENSORS (live dashboard + SD logging),
 *          IMU LOGGER (BNO085 + web page) and SETTINGS
 * In game: touch X pauses; while paused X resumes, touch Left quits
 *
 * Every game has its own top 5 on the micro SD card, and every run is
 * appended to /arcade/runs.csv.
 */

#include <Arduino.h>

#include "game.h"
#include "scores.h"
#include "settings.h"
#include "dashboard.h"
#include "splash.h"
#include "imulogger.h"

static const Game *const games[] = {
  &orbitGame,
  &cometGame,
  &breakerGame,
  &marbleGame,
  &caveGame,
  &patrolGame,
  &cargoGame,
  &tankGame,
  &duelGame,
};
static const int numGames = sizeof(games) / sizeof(games[0]);
static const char *gameIds[numGames];
static const int sensorsEntry = numGames;     // apps come after the games
static const int loggerEntry = numGames + 1;
static const int setupEntry = numGames + 2;
static const int numEntries = numGames + 3;

const unsigned long frameMs = 33;          // ~30 fps
const int menuRows = 4;

enum State { SPLASH, MENU, SCORES, PLAYING, PAUSED, GAME_OVER, ENTER_NAME, SETTINGS, SENSORS, LOGGER };

static State state = SPLASH;
static unsigned long stateSince;
static int selected = 0;
static int menuTop = 0;

static uint32_t lastScore;
static uint32_t lastSeconds;
static char lastSummary[32];
static char nameEntry[4] = "AAA";
static int nameCursor;

static void setState(State s){
  state = s;
  stateSince = millis();
}

static void resetLights(){
  rgbLed(1, false);
  rgbLed(3, false);
  statusLed(false);
  display.invertDisplay(false);
}

// ---------------------------------------------------------------- menu

static void drawMenu(){
  display.clearDisplay();
  updateStars(0.4f, 11);
  drawStars();

  display.fillRect(0, 0, screenW, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(2, 1);
  display.print("CADSE ARCADE");
  display.setCursor(84, 1);
  display.print(sdAvailable() ? "SD" : "");
  drawBatteryIcon(screenW - 21, 1, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);

  for( int row = 0; row < menuRows && menuTop + row < numEntries; row++ ){
    int i = menuTop + row;
    int y = 12 + row * 10;
    if( i == selected ){
      display.fillRect(0, y, screenW - 4, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.fillRect(0, y, screenW - 4, 10, SSD1306_BLACK);
    }
    display.setCursor(4, y + 1);
    display.print(i == setupEntry ? "SETTINGS" : i == sensorsEntry ? "SENSORS" : i == loggerEntry ? "IMU LOGGER" : games[i]->title);
    display.setTextColor(SSD1306_WHITE);
  }

  // scroll bar
  int barH = 40 * menuRows / numEntries;
  int barY = 12 + (40 - barH) * selected / max(1, numEntries - 1);
  display.drawFastVLine(screenW - 2, barY, barH, SSD1306_WHITE);

  // bottom line alternates between help and best score
  display.fillRect(0, 54, screenW, 10, SSD1306_BLACK);
  if( selected == loggerEntry ){
    centerText("BNO085, web, SD log", 55);
    display.display();
    return;
  }
  if( selected == sensorsEntry ){
    centerText("live data, SD logging", 55);
    display.display();
    return;
  }
  if( selected == setupEntry ){
    centerText("sound, LEDs, tilt", 55);
    display.display();
    return;
  }
  const HighScore &best = highScores(selected)[0];
  if( (millis() / 2500) % 2 == 0 || best.score == 0 ){
    centerText(games[selected]->help, 55);
  } else {
    char line[24];
    snprintf(line, sizeof(line), "best %s %lu", best.name, (unsigned long)best.score);
    centerText(line, 55);
  }
  display.display();
}

static void moveSelection(int delta){
  selected = (selected + delta + numEntries) % numEntries;
  if( selected < menuTop ) menuTop = selected;
  if( selected >= menuTop + menuRows ) menuTop = selected - menuRows + 1;
  playSfx(sfxClick);
}

static void startSelected(){
  resetLights();
  gameClockReset();
  games[selected]->start();
  playSfx(sfxStart);
  setState(PLAYING);
}

static void drawScores(){
  display.clearDisplay();
  centerText(games[selected]->title, 0);
  display.drawFastHLine(0, 9, screenW, SSD1306_WHITE);
  const HighScore *table = highScores(selected);
  for( int i = 0; i < numHighScores; i++ ){
    display.setCursor(22, 13 + i * 10);
    display.printf("%d  %s  %6lu", i + 1, table[i].name, (unsigned long)table[i].score);
  }
  display.display();
}

// ---------------------------------------------------------------- game over

static void finishGame(){
  lastScore = games[selected]->score();
  lastSeconds = gameMillis() / 1000;
  lastSummary[0] = '\0';
  if( games[selected]->summary ){
    games[selected]->summary(lastSummary, sizeof(lastSummary));
  }
  resetLights();
  playSfx(sfxGameOver);
  setState(GAME_OVER);
}

static void drawGameOver(){
  display.clearDisplay();
  char line[24];
  if( games[selected]->versus ){
    centerText(games[selected]->title, 0);
    centerText(lastSummary, 22, 2);
    snprintf(line, sizeof(line), "%lu s", (unsigned long)lastSeconds);
    centerText(line, 44);
    if( millis() - stateSince > 1500 && (millis() / 500) % 2 ){
      centerText("touch X", 56);
    }
    display.display();
    return;
  }
  centerText("GAME OVER", 0, 1);
  centerText(games[selected]->title, 10);
  snprintf(line, sizeof(line), "%lu", (unsigned long)lastScore);
  centerText(line, 22, 2);
  snprintf(line, sizeof(line), "%lu s", (unsigned long)lastSeconds);
  centerText(line, 40);
  centerText(lastSummary, 48);
  if( millis() - stateSince > 1500 && (millis() / 500) % 2 ){
    display.fillRect(0, 56, screenW, 8, SSD1306_BLACK);
    centerText("touch X", 56);
  }
  display.display();
}

static void drawEnterName(){
  display.clearDisplay();
  centerText("NEW HIGH SCORE!", 0);
  char line[16];
  snprintf(line, sizeof(line), "%lu", (unsigned long)lastScore);
  centerText(line, 11);
  for( int i = 0; i < 3; i++ ){
    int x = 37 + i * 20;
    display.setTextSize(2);
    display.setCursor(x, 26);
    display.print(nameEntry[i]);
    if( i == nameCursor ){
      display.drawFastHLine(x, 43, 11, SSD1306_WHITE);
    }
  }
  display.setTextSize(1);
  centerText("up/down  left/right", 48);
  centerText("X to save", 56);
  display.display();
}

static void updateEnterName(const Buttons &pressed){
  char &c = nameEntry[nameCursor];
  if( pressed.up ){
    c = (c == 'Z') ? 'A' : c + 1;
    playSfx(sfxClick);
  }
  if( pressed.down ){
    c = (c == 'A') ? 'Z' : c - 1;
    playSfx(sfxClick);
  }
  if( pressed.right && nameCursor < 2 ){
    nameCursor++;
    playSfx(sfxClick);
  }
  if( pressed.left && nameCursor > 0 ){
    nameCursor--;
    playSfx(sfxClick);
  }
  if( pressed.x ){
    addHighScore(selected, nameEntry, lastScore);
    logRun(selected, nameEntry, lastScore, lastSeconds, lastSummary);
    playSfx(sfxPickup);
    setState(SCORES);
  }
}

// ---------------------------------------------------------------- main

void setup(){
  Serial.begin(115200);
  delay(500);
  Serial.println("CADSE ARCADE starting ...");
  randomSeed(esp_random());
  hwBegin();
  logTouchValues();
  for( int i = 0; i < numGames; i++ ){
    gameIds[i] = games[i]->id;
  }
  scoresBegin(gameIds, numGames);
  display.setTextColor(SSD1306_WHITE);
  initStars();
  splashStart();
  setState(SPLASH);
  Serial.println(imuAvailable() ? "Tilt steering ready" : "IMU missing, touch steering only");
}

void loop(){
  static unsigned long lastFrame = 0;
  unsigned long now = millis();
  updateSfx();
  if( state == LOGGER ){
    loggerService();      // sensor, SD and web work runs between frames
  }
  if( now - lastFrame < frameMs ){
    return;
  }
  lastFrame = now;

  Buttons held, pressed;
  readButtons(held, pressed);
  updatePower();

  switch( state ){
    case SPLASH:
      if( !splashUpdate(pressed) ){
        setState(MENU);
        break;
      }
      splashDraw();
      break;

    case MENU:
      rgbLed(3, (now / 600) % 2);
      if( pressed.up ) moveSelection(-1);
      if( pressed.down || pressed.shield ) moveSelection(1);
      if( pressed.right && selected < numGames ){
        playSfx(sfxClick);
        setState(SCORES);
      }
      if( (pressed.x || pressed.fire) && selected == loggerEntry ){
        resetLights();
        loggerStart();
        playSfx(sfxClick);
        setState(LOGGER);
        break;
      }
      if( (pressed.x || pressed.fire) && selected == sensorsEntry ){
        resetLights();
        dashboardStart();
        playSfx(sfxClick);
        setState(SENSORS);
        break;
      }
      if( (pressed.x || pressed.fire) && selected == setupEntry ){
        resetLights();
        settingsStart();
        playSfx(sfxClick);
        setState(SETTINGS);
        break;
      }
      if( pressed.x || pressed.fire ){
        startSelected();
        break;
      }
      drawMenu();
      break;

    case SCORES:
      // apps have no scores, skip over them
      if( pressed.up ){ do { moveSelection(-1); } while( selected >= numGames ); }
      if( pressed.down ){ do { moveSelection(1); } while( selected >= numGames ); }
      if( pressed.x || pressed.left || pressed.fire ){
        playSfx(sfxClick);
        setState(MENU);
      }
      drawScores();
      break;

    case PLAYING:
      if( pressed.x && !games[selected]->online ){
        gameClockPause();
        playSfx(sfxClick);
        setState(PAUSED);
        break;
      }
      if( !games[selected]->update(held, pressed) ){
        finishGame();
        break;
      }
      games[selected]->draw();
      display.display();
      break;

    case PAUSED:
      if( pressed.x ){
        gameClockResume();
        if( games[selected]->resume ){
          games[selected]->resume();
        }
        playSfx(sfxClick);
        setState(PLAYING);
        break;
      }
      if( pressed.left ){
        resetLights();
        playSfx(sfxClick);
        setState(MENU);
        break;
      }
      games[selected]->draw();
      display.fillRect(24, 20, 80, 24, SSD1306_BLACK);
      display.drawRect(24, 20, 80, 24, SSD1306_WHITE);
      centerText("PAUSED", 24);
      centerText("X play  < quit", 34);
      display.display();
      break;

    case GAME_OVER:
      rgbLed(1, (now / 300) % 2);
      if( pressed.x && now - stateSince > 1500 ){
        rgbLed(1, false);
        if( !games[selected]->versus && isHighScore(selected, lastScore) ){
          nameCursor = 0;
          setState(ENTER_NAME);
        } else {
          logRun(selected, "---", lastScore, lastSeconds, lastSummary);
          setState(MENU);
        }
        break;
      }
      drawGameOver();
      break;

    case LOGGER:
      if( !loggerUpdate(held, pressed) ){
        setState(MENU);
        break;
      }
      loggerDraw();
      break;

    case SENSORS:
      if( !dashboardUpdate(held, pressed) ){
        setState(MENU);
        break;
      }
      dashboardDraw();
      break;

    case SETTINGS:
      if( !settingsUpdate(held, pressed) ){
        setState(MENU);
        break;
      }
      settingsDraw();
      break;

    case ENTER_NAME:
      updateEnterName(pressed);
      if( state == ENTER_NAME ){
        drawEnterName();
      }
      break;
  }
}
