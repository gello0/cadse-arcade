/* High score tables and run log on the micro SD card
 *
 * /arcade/<game id>.txt  top 5 per game, one "AAA 1234" per line
 * /arcade/runs.csv       every finished run of every game
 * Without a card the tables only live in RAM until power off.
 */
#pragma once

#include <Arduino.h>

const int numHighScores = 5;
const int maxGames = 12;

struct HighScore {
  char name[4];
  uint32_t score;
};

bool scoresBegin(const char *const ids[], int count);  // mounts the card, loads all tables
bool sdAvailable();
const HighScore *highScores(int game);
bool isHighScore(int game, uint32_t score);
void addHighScore(int game, const char *name, uint32_t score);
void logRun(int game, const char *name, uint32_t score, uint32_t seconds, const char *details);
