#include "onboardimu.h"
#include "hw.h"

const float filterAlpha = 0.98f;           // share of the gyro in the blend
const unsigned long minStepUs = 5000;      // filter runs at most at 200 Hz
const float rad2deg = 57.29578f;

static OnboardSample latest = {};
static unsigned long lastUs = 0;
static bool started = false;

void onboardReset(){
  started = false;
  latest.ypr[0] = latest.ypr[1] = latest.ypr[2] = 0;
}

static void eulerToQuat(const float ypr[3], float q[4]){
  float cy = cosf(ypr[0] / rad2deg / 2), sy = sinf(ypr[0] / rad2deg / 2);
  float cp = cosf(ypr[1] / rad2deg / 2), sp = sinf(ypr[1] / rad2deg / 2);
  float cr = cosf(ypr[2] / rad2deg / 2), sr = sinf(ypr[2] / rad2deg / 2);
  q[0] = cr * cp * cy + sr * sp * sy;
  q[1] = sr * cp * cy - cr * sp * sy;
  q[2] = cr * sp * cy + sr * cp * sy;
  q[3] = cr * cp * sy - sr * sp * cy;
}

void onboardPoll(){
  if( !imuAvailable() ){
    return;
  }
  unsigned long nowUs = micros();
  if( started && nowUs - lastUs < minStepUs ){
    return;
  }
  float dt = started ? (nowUs - lastUs) / 1e6f : 0;
  lastUs = nowUs;

  readImu(latest.acc, latest.gyr, latest.tempC);
  const float *a = latest.acc;
  float accRoll = atan2f(a[1], a[2]) * rad2deg;
  float accPitch = atan2f(-a[0], sqrtf(a[1] * a[1] + a[2] * a[2])) * rad2deg;

  if( !started ){
    latest.ypr[0] = 0;
    latest.ypr[1] = accPitch;
    latest.ypr[2] = accRoll;
    started = true;
  } else {
    // roll can jump between +180 and -180, blend across the wrap
    float gyroRoll = latest.ypr[2] + latest.gyr[0] * rad2deg * dt;
    float diff = accRoll - gyroRoll;
    if( diff > 180 ) diff -= 360;
    if( diff < -180 ) diff += 360;
    latest.ypr[2] = gyroRoll + (1 - filterAlpha) * diff;
    if( latest.ypr[2] > 180 ) latest.ypr[2] -= 360;
    if( latest.ypr[2] < -180 ) latest.ypr[2] += 360;

    float gyroPitch = latest.ypr[1] + latest.gyr[1] * rad2deg * dt;
    latest.ypr[1] = filterAlpha * gyroPitch + (1 - filterAlpha) * accPitch;

    latest.ypr[0] += latest.gyr[2] * rad2deg * dt;   // no compass: yaw drifts
    if( latest.ypr[0] > 180 ) latest.ypr[0] -= 360;
    if( latest.ypr[0] < -180 ) latest.ypr[0] += 360;
  }
  eulerToQuat(latest.ypr, latest.q);
  latest.ms = millis();
}

const OnboardSample &onboardLatest(){
  return latest;
}
