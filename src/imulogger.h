/* IMU LOGGER app: BNO085 + onboard IMU, SD logging and a live web page */
#pragma once

#include "hw.h"

void loggerStart();
void loggerService();                                            // every loop pass
bool loggerUpdate(const Buttons &held, const Buttons &pressed);  // false = back to menu
void loggerDraw();
