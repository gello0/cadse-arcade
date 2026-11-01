#include "bno.h"

#include <Wire.h>
#include <Adafruit_BNO08x.h>

const uint32_t fusionIntervalUs = 10000;   // 100 Hz reports, the logger samples from these
const uint32_t magIntervalUs = 20000;

static Adafruit_BNO08x bno(bnoResetPin);   // harmless if RST isn't wired
static bool bnoOk = false;
static ImuSample latest = {};
static uint32_t rateCount = 0;
static unsigned long rateSince = 0;
static float rateHz = 0;

static void enableReports(){
  bno.enableReport(SH2_ROTATION_VECTOR, fusionIntervalUs);
  bno.enableReport(SH2_LINEAR_ACCELERATION, fusionIntervalUs);
  bno.enableReport(SH2_GYROSCOPE_CALIBRATED, fusionIntervalUs);
  bno.enableReport(SH2_MAGNETIC_FIELD_CALIBRATED, magIntervalUs);
}

bool bnoBegin(){
  if( bnoOk ){
    return true;
  }
  bnoOk = bno.begin_I2C(0x4A, &Wire) || bno.begin_I2C(0x4B, &Wire);
  if( bnoOk ){
    enableReports();
    rateSince = millis();
    Serial.println("BNO085 found");
  } else {
    Serial.println("BNO085 not found on I2C 0x4A/0x4B");
  }
  return bnoOk;
}

bool bnoAvailable(){
  return bnoOk;
}

static void toEuler(const float q[4], float ypr[3]){
  float w = q[0], x = q[1], y = q[2], z = q[3];
  const float rad2deg = 57.29578f;
  ypr[0] = atan2f(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)) * rad2deg;
  float s = constrain(2 * (w * y - z * x), -1.0f, 1.0f);
  ypr[1] = asinf(s) * rad2deg;
  ypr[2] = atan2f(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) * rad2deg;
}

void bnoPoll(){
  if( !bnoOk ){
    return;
  }
  if( bno.wasReset() ){
    enableReports();    // the sensor forgets its reports after a reset
  }
  sh2_SensorValue_t v;
  for( int n = 0; n < 16 && bno.getSensorEvent(&v); n++ ){
    switch( v.sensorId ){
      case SH2_ROTATION_VECTOR:
        latest.q[0] = v.un.rotationVector.real;
        latest.q[1] = v.un.rotationVector.i;
        latest.q[2] = v.un.rotationVector.j;
        latest.q[3] = v.un.rotationVector.k;
        latest.accuracy = v.status & 0x03;
        latest.ms = millis();
        toEuler(latest.q, latest.ypr);
        rateCount++;
        break;
      case SH2_LINEAR_ACCELERATION:
        latest.lin[0] = v.un.linearAcceleration.x;
        latest.lin[1] = v.un.linearAcceleration.y;
        latest.lin[2] = v.un.linearAcceleration.z;
        break;
      case SH2_GYROSCOPE_CALIBRATED:
        latest.gyr[0] = v.un.gyroscope.x;
        latest.gyr[1] = v.un.gyroscope.y;
        latest.gyr[2] = v.un.gyroscope.z;
        break;
      case SH2_MAGNETIC_FIELD_CALIBRATED:
        latest.mag[0] = v.un.magneticField.x;
        latest.mag[1] = v.un.magneticField.y;
        latest.mag[2] = v.un.magneticField.z;
        break;
    }
  }
  unsigned long now = millis();
  if( now - rateSince >= 1000 ){
    rateHz = rateCount * 1000.0f / (now - rateSince);
    rateCount = 0;
    rateSince = now;
  }
}

const ImuSample &bnoLatest(){
  return latest;
}

float bnoRateHz(){
  return rateHz;
}
