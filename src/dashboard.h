/* SENSORS app, reached from the menu: live readings of every sensor on the
 * CADSE board and CSV logging to the micro SD card
 */
#pragma once

#include "hw.h"

void dashboardStart();
bool dashboardUpdate(const Buttons &held, const Buttons &pressed);  // false = back to menu
void dashboardDraw();
