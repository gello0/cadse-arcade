#include "scores.h"
#include "hw.h"

#include <FS.h>
#include <SD_MMC.h>

static HighScore tables[maxGames][numHighScores];
static const char *const *gameIds = nullptr;
static int numGames = 0;
static bool sdOk = false;
static const char *runsPath = "/arcade/runs.csv";

static String scoresPath(int game){
  return String("/arcade/") + gameIds[game] + ".txt";
}

static void saveScores(int game){
  if( !sdOk ){
    return;
  }
  File f = SD_MMC.open(scoresPath(game), FILE_WRITE);
  if( !f ){
    Serial.println("Could not write scores");
    return;
  }
  for( int i = 0; i < numHighScores; i++ ){
    f.printf("%s %lu\n", tables[game][i].name, (unsigned long)tables[game][i].score);
  }
  f.close();
}

static void loadScores(int game){
  HighScore *table = tables[game];
  for( int i = 0; i < numHighScores; i++ ){
    strcpy(table[i].name, "---");
    table[i].score = 0;
  }
  String path = scoresPath(game);
  if( !sdOk || !SD_MMC.exists(path) ){
    return;
  }
  File f = SD_MMC.open(path, FILE_READ);
  for( int i = 0; i < numHighScores && f.available(); i++ ){
    String line = f.readStringUntil('\n');
    int space = line.indexOf(' ');
    if( space < 1 ){
      break;
    }
    strncpy(table[i].name, line.substring(0, space).c_str(), 3);
    table[i].name[3] = '\0';
    table[i].score = line.substring(space + 1).toInt();
  }
  f.close();
}

bool scoresBegin(const char *const ids[], int count){
  gameIds = ids;
  numGames = min(count, maxGames);

  SD_MMC.setPins(sdClkPin, sdCmdPin, sdD0Pin, sdD1Pin, sdD2Pin, sdD3Pin);
  sdOk = SD_MMC.begin("/sdcard", false);
  if( sdOk ){
    if( !SD_MMC.exists("/arcade") ){
      SD_MMC.mkdir("/arcade");
    }
    Serial.printf("SD card mounted, %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));
  } else {
    Serial.println("No SD card, high scores are not saved");
  }
  for( int g = 0; g < numGames; g++ ){
    loadScores(g);
  }
  return sdOk;
}

bool sdAvailable(){
  return sdOk;
}

const HighScore *highScores(int game){
  return tables[game];
}

bool isHighScore(int game, uint32_t score){
  return score > tables[game][numHighScores - 1].score;
}

void addHighScore(int game, const char *name, uint32_t score){
  HighScore *table = tables[game];
  int pos = numHighScores - 1;
  while( pos > 0 && table[pos - 1].score < score ){
    table[pos] = table[pos - 1];
    pos--;
  }
  strncpy(table[pos].name, name, 3);
  table[pos].name[3] = '\0';
  table[pos].score = score;
  saveScores(game);
}

void logRun(int game, const char *name, uint32_t score, uint32_t seconds, const char *details){
  if( !sdOk ){
    return;
  }
  bool isNew = !SD_MMC.exists(runsPath);
  File f = SD_MMC.open(runsPath, FILE_APPEND);
  if( !f ){
    return;
  }
  if( isNew ){
    f.println("game,name,score,seconds,details");
  }
  f.printf("%s,%s,%lu,%lu,\"%s\"\n", gameIds[game], name, (unsigned long)score,
           (unsigned long)seconds, details);
  f.close();
}
