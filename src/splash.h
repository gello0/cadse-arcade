/* Boot splash in several styles, chosen in SETTINGS and saved to flash */
#pragma once

#include "hw.h"

enum SplashStyle { SPLASH_WARP, SPLASH_ORBIT, SPLASH_BOOT, SPLASH_CIRCUIT, SPLASH_OFF, NUM_SPLASH_STYLES };

extern const char *const splashStyleNames[NUM_SPLASH_STYLES];
int splashStyle();
void setSplashStyle(int style);

void splashStart();                          // uses the saved style
void splashStart(int style);                 // for the preview in SETTINGS
bool splashUpdate(const Buttons &pressed);   // false = done
void splashDraw();
