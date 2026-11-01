#include "settings.h"
#include "calibrate.h"
#include "game.h"
#include "splash.h"

enum SettingsItem { ITEM_VOLUME, ITEM_LEDS, ITEM_CHANNEL, ITEM_SPLASH, ITEM_TILT, ITEM_BACK, NUM_ITEMS };
const int visibleItems = 5;

static int item;
static int topItem;
static bool calibrating;
static bool previewing;

static void previewLeds(bool on){
  rgbLed(1, on);
  rgbLed(3, on);
  statusLed(on);
}

void settingsStart(){
  item = ITEM_VOLUME;
  topItem = 0;
  calibrating = false;
  previewing = false;
}

static bool leave(){
  previewLeds(false);
  playSfx(sfxClick);
  return false;
}

bool settingsUpdate(const Buttons &held, const Buttons &pressed){
  if( calibrating ){
    calibrating = calibUpdate(held, pressed);
    return true;
  }
  if( previewing ){
    previewing = splashUpdate(pressed);
    return true;
  }

  if( pressed.up ){
    item = (item + NUM_ITEMS - 1) % NUM_ITEMS;
    playSfx(sfxClick);
  }
  if( pressed.down ){
    item = (item + 1) % NUM_ITEMS;
    playSfx(sfxClick);
  }
  if( item < topItem ) topItem = item;
  if( item >= topItem + visibleItems ) topItem = item - visibleItems + 1;

  bool less = pressed.left || pressed.fire;
  bool more = pressed.right || pressed.shield;

  switch( item ){
    case ITEM_VOLUME:
      // touch Left/Right or the bumpers change the volume, with a test beep
      if( less && getVolume() > 0 ){
        setVolume(getVolume() - 1);
        playSfx(sfxPickup);
      }
      if( more && getVolume() < numVolumeLevels - 1 ){
        setVolume(getVolume() + 1);
        playSfx(sfxPickup);
      }
      if( pressed.x ) return leave();
      break;

    case ITEM_LEDS:
      if( less && ledMode > 0 ){
        setLedConfig(ledMode - 1, ledChannel);
        playSfx(sfxClick);
      }
      if( more && ledMode < NUM_LED_MODES - 1 ){
        setLedConfig(ledMode + 1, ledChannel);
        playSfx(sfxClick);
      }
      if( pressed.x ) return leave();
      break;

    case ITEM_CHANNEL:
      // try each channel and keep one that lights up in the colour you like
      if( less ){
        setLedConfig(ledMode, (ledChannel + 3) % 4);
        playSfx(sfxClick);
      }
      if( more ){
        setLedConfig(ledMode, (ledChannel + 1) % 4);
        playSfx(sfxClick);
      }
      if( pressed.x ) return leave();
      break;

    case ITEM_SPLASH:
      if( less ){
        setSplashStyle((splashStyle() + NUM_SPLASH_STYLES - 1) % NUM_SPLASH_STYLES);
        playSfx(sfxClick);
      }
      if( more ){
        setSplashStyle((splashStyle() + 1) % NUM_SPLASH_STYLES);
        playSfx(sfxClick);
      }
      if( pressed.x && splashStyle() != SPLASH_OFF ){
        previewLeds(false);
        splashStart();
        previewing = true;
        return true;
      }
      break;

    case ITEM_TILT:
      if( pressed.x || pressed.right ){
        previewLeds(false);
        calibStart();
        calibrating = true;
        playSfx(sfxClick);
        return true;
      }
      if( pressed.left ) return leave();
      break;

    case ITEM_BACK:
      if( pressed.x || pressed.left ) return leave();
      break;
  }

  previewLeds(item == ITEM_LEDS || item == ITEM_CHANNEL);
  return true;
}

void settingsDraw(){
  if( calibrating ){
    calibDraw();
    return;
  }
  if( previewing ){
    splashDraw();
    return;
  }

  display.clearDisplay();
  display.fillRect(0, 0, screenW, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(2, 1);
  display.print("SETTINGS");

  // battery status in the title bar
  char power[16];
  switch( powerState() ){
    case CHARGING: snprintf(power, sizeof(power), "CHG %d%%", batteryPercent()); break;
    case CHARGED:  snprintf(power, sizeof(power), "FULL %.2fV", batteryVolts()); break;
    default:       snprintf(power, sizeof(power), "%d%% %.2fV", batteryPercent(), batteryVolts()); break;
  }
  display.setCursor(screenW - 2 - strlen(power) * 6, 1);
  display.print(power);
  display.setTextColor(SSD1306_WHITE);

  const char *labels[NUM_ITEMS] = {"VOLUME", "LEDS", "LOW COLOR", "SPLASH", "TILT SETUP", "BACK"};
  const char *modeNames[NUM_LED_MODES] = {"OFF", "LOW", "HIGH"};
  for( int i = topItem; i < NUM_ITEMS && i < topItem + visibleItems; i++ ){
    int y = 11 + (i - topItem) * 9;
    bool sel = i == item;
    uint16_t fg = sel ? SSD1306_BLACK : SSD1306_WHITE;
    if( sel ){
      display.fillRect(0, y, screenW, 9, SSD1306_WHITE);
    }
    display.setTextColor(fg);
    display.setCursor(4, y + 1);
    display.print(labels[i]);

    if( i == ITEM_VOLUME ){
      int v = getVolume();
      if( v == 0 ){
        display.setCursor(80, y + 1);
        display.print("OFF");
      }
      for( int b = 1; b < numVolumeLevels; b++ ){
        int h = 1 + b * 2;
        int x = 100 + b * 6;
        if( b <= v ){
          display.fillRect(x, y + 8 - h, 4, h, fg);
        } else {
          display.drawFastHLine(x, y + 7, 4, fg);
        }
      }
    } else if( i == ITEM_LEDS ){
      display.setCursor(screenW - 4 - strlen(modeNames[ledMode]) * 6, y + 1);
      display.print(modeNames[ledMode]);
    } else if( i == ITEM_SPLASH ){
      const char *name = splashStyleNames[splashStyle()];
      display.setCursor(screenW - 4 - strlen(name) * 6, y + 1);
      display.print(name);
    } else if( i == ITEM_CHANNEL ){
      display.setCursor(screenW - 4 - 6 * 6, y + 1);
      display.printf("pin %d", ledChannel + 1);
      if( ledMode != LEDS_LOW && sel ){
        display.setCursor(64, y + 1);
        display.print("(LOW)");
      }
    }
    display.setTextColor(SSD1306_WHITE);
  }

  const char *hint = "";
  switch( item ){
    case ITEM_VOLUME:  hint = "< quieter  louder >"; break;
    case ITEM_LEDS:    hint = "< dimmer  brighter >"; break;
    case ITEM_CHANNEL: hint = "< pick lit channel >"; break;
    case ITEM_SPLASH:  hint = "< style >  X preview"; break;
    case ITEM_TILT:    hint = "X to calibrate"; break;
    case ITEM_BACK:    hint = "X back to menu"; break;
  }
  centerText(hint, 57);
  display.display();
}
