/* SETTINGS screen, reached from the last entry of the menu:
 * volume, LED brightness and tilt calibration, all saved to flash
 */
#pragma once

#include "hw.h"

void settingsStart();
bool settingsUpdate(const Buttons &held, const Buttons &pressed);  // false = back to menu
void settingsDraw();
