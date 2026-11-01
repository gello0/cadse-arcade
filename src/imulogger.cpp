/* IMU LOGGER
 *
 *   Left bumper      start / stop logging to the SD card
 *   Touch Up/Down    logging rate 10 / 25 / 50 / 100 Hz (while stopped)
 *   Touch Right      switch the screen between BNO085 and onboard IMU
 *   Touch Left       retry finding the BNO085
 *   Touch X          back to the menu (stops logging and Wi-Fi)
 *
 * While the app is open the board runs a Wi-Fi access point with the live
 * page, see webui.h.
 */
#include "imulogger.h"
#include "bno.h"
#include "onboardimu.h"
#include "imulog.h"
#include "webui.h"
#include "game.h"
#include "scores.h"

#include <WiFi.h>

static int rateIndex = 2;          // 50 Hz
static bool showOnboard = false;

void loggerStart(){
  bnoBegin();
  onboardReset();
  webStart();
  showOnboard = !bnoAvailable();
}

void loggerService(){
  bnoPoll();
  onboardPoll();
  imuLogService();
  webService();
}

bool loggerUpdate(const Buttons &held, const Buttons &pressed){
  if( pressed.x ){
    imuLogStop();
    webStop();
    rgbLed(1, false);
    rgbLed(3, false);
    return false;
  }
  if( pressed.fire ){
    if( imuLogActive() ){
      imuLogStop();
      playSfx(sfxClick);
    } else if( imuLogStart(imuLogRates[rateIndex]) ){
      playSfx(sfxPickup);
    } else {
      playSfx(sfxHit);   // no SD card
    }
  }
  if( !imuLogActive() ){
    if( pressed.up && rateIndex < numImuLogRates - 1 ){ rateIndex++; playSfx(sfxClick); }
    if( pressed.down && rateIndex > 0 ){ rateIndex--; playSfx(sfxClick); }
  }
  if( pressed.right ){
    showOnboard = !showOnboard;
    playSfx(sfxClick);
  }
  if( pressed.left && !bnoAvailable() ){
    if( bnoBegin() ){
      showOnboard = false;
      playSfx(sfxPickup);
    } else {
      playSfx(sfxHit);
    }
  }
  rgbLed(1, imuLogActive() && (millis() / 500) % 2);
  rgbLed(3, webClients() > 0);
  return true;
}

void loggerDraw(){
  display.clearDisplay();
  display.setTextSize(1);
  display.fillRect(0, 0, screenW, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(showOnboard ? "ONBOARD IMU" : "BNO085");
  if( imuLogActive() && (millis() / 500) % 2 ){
    display.setCursor(screenW - 20, 1);
    display.print("REC");
  }
  display.setTextColor(SSD1306_WHITE);

  if( !showOnboard && !bnoAvailable() ){
    display.setCursor(0, 12);
    display.print("BNO085 not found");
    display.setCursor(0, 21);
    display.print("SDA H2-17  SCL H2-18");
    display.setCursor(0, 30);
    display.print("3V3 H1-5   GND H1-1");
    display.setCursor(0, 39);
    display.print("< retry   > onboard");
  } else {
    const float *ypr;
    char extra[22];
    if( showOnboard ){
      const OnboardSample &o = onboardLatest();
      ypr = o.ypr;
      snprintf(extra, sizeof(extra), "%.1fC yaw drifts", o.tempC);
    } else {
      const ImuSample &b = bnoLatest();
      ypr = b.ypr;
      snprintf(extra, sizeof(extra), "acc %d/3  %3.0f Hz", b.accuracy, bnoRateHz());
    }
    display.setCursor(0, 12);
    display.printf("Y%6.1f P%6.1f", ypr[0], ypr[1]);
    display.setCursor(0, 21);
    display.printf("R%6.1f %s", ypr[2], extra);
  }

  display.setCursor(0, 30);
  if( imuLogActive() ){
    display.printf("REC %dHz %lu", imuLogRate(), (unsigned long)imuLogLines());
  } else {
    display.printf("log off  %dHz %s", imuLogRates[rateIndex], sdAvailable() ? "" : "noSD");
  }

  // how to reach the web page
  display.drawFastHLine(0, 39, screenW, SSD1306_WHITE);
  display.setCursor(0, 41);
  display.printf("%s  %d conn", webSsid(), webClients());
  display.setCursor(0, 49);
  display.print("open Wi-Fi, no pw");
  display.setCursor(0, 57);
  display.printf("http://%s", WiFi.softAPIP().toString().c_str());
  display.display();
}
