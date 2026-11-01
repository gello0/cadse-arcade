#include "imulog.h"
#include "bno.h"
#include "onboardimu.h"
#include "scores.h"

#include <SD_MMC.h>

const int imuLogRates[] = {10, 25, 50, 100};
const int numImuLogRates = sizeof(imuLogRates) / sizeof(imuLogRates[0]);

const size_t logBufferSize = 8192;
const size_t flushAt = 6144;
const unsigned long flushEveryMs = 1000;

static File logFile;
static bool active = false;
static int rateHz = 50;
static uint32_t nextSampleUs;
static char buffer[logBufferSize];
static size_t bufferLen = 0;
static unsigned long lastFlush;
static uint32_t lines;
static char fileName[20] = "";

static void flushBuffer(){
  if( bufferLen == 0 ){
    return;
  }
  logFile.write((const uint8_t *)buffer, bufferLen);
  logFile.flush();     // so a pulled card or power loss loses at most a second
  bufferLen = 0;
  lastFlush = millis();
}

bool imuLogStart(int hz){
  if( active || !sdAvailable() ){
    return active;
  }
  if( !SD_MMC.exists(imuLogDir) ){
    SD_MMC.mkdir(imuLogDir);
  }
  char path[32];
  for( int n = 1; n < 1000; n++ ){
    snprintf(fileName, sizeof(fileName), "log_%03d.csv", n);
    snprintf(path, sizeof(path), "%s/%s", imuLogDir, fileName);
    if( !SD_MMC.exists(path) ) break;
  }
  logFile = SD_MMC.open(path, FILE_WRITE);
  if( !logFile ){
    return false;
  }
  logFile.println("t_ms,bno_qw,bno_qx,bno_qy,bno_qz,bno_yaw,bno_pitch,bno_roll,"
                  "bno_lin_x,bno_lin_y,bno_lin_z,bno_gyr_x,bno_gyr_y,bno_gyr_z,"
                  "bno_mag_x,bno_mag_y,bno_mag_z,bno_accuracy,"
                  "ob_acc_x,ob_acc_y,ob_acc_z,ob_gyr_x,ob_gyr_y,ob_gyr_z,"
                  "ob_yaw,ob_pitch,ob_roll");
  rateHz = hz;
  lines = 0;
  bufferLen = 0;
  lastFlush = millis();
  nextSampleUs = micros();
  active = true;
  Serial.printf("IMU log started: %s at %d Hz\n", path, hz);
  return true;
}

void imuLogStop(){
  if( !active ){
    return;
  }
  flushBuffer();
  logFile.close();
  active = false;
  Serial.printf("IMU log stopped: %lu lines\n", (unsigned long)lines);
}

static void appendLine(){
  if( bufferLen > logBufferSize - 400 ){
    flushBuffer();
  }
  char *p = buffer + bufferLen;
  size_t room = logBufferSize - bufferLen;
  int n = snprintf(p, room, "%lu,", (unsigned long)millis());

  if( bnoAvailable() ){
    const ImuSample &b = bnoLatest();
    n += snprintf(p + n, room - n,
                  "%.5f,%.5f,%.5f,%.5f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f,%d,",
                  b.q[0], b.q[1], b.q[2], b.q[3], b.ypr[0], b.ypr[1], b.ypr[2],
                  b.lin[0], b.lin[1], b.lin[2], b.gyr[0], b.gyr[1], b.gyr[2],
                  b.mag[0], b.mag[1], b.mag[2], b.accuracy);
  } else {
    n += snprintf(p + n, room - n, ",,,,,,,,,,,,,,,,,");   // no BNO085: empty columns
  }

  const OnboardSample &o = onboardLatest();
  n += snprintf(p + n, room - n, "%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.2f,%.2f,%.2f\n",
                o.acc[0], o.acc[1], o.acc[2], o.gyr[0], o.gyr[1], o.gyr[2],
                o.ypr[0], o.ypr[1], o.ypr[2]);
  bufferLen += n;
  lines++;
}

void imuLogService(){
  if( !active ){
    return;
  }
  uint32_t now = micros();
  uint32_t period = 1000000UL / rateHz;
  if( (int32_t)(now - nextSampleUs) >= 0 ){
    appendLine();
    nextSampleUs += period;
    // after a long stall (e.g. a big download) skip ahead instead of bursting
    if( (int32_t)(now - nextSampleUs) > 500000 ){
      nextSampleUs = now + period;
    }
  }
  if( bufferLen >= flushAt || millis() - lastFlush >= flushEveryMs ){
    flushBuffer();
  }
}

bool imuLogActive(){
  return active;
}

int imuLogRate(){
  return rateHz;
}

const char *imuLogFile(){
  return fileName;
}

uint32_t imuLogLines(){
  return lines;
}
