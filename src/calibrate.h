/* TILT SETUP screen, reached from the last entry of the menu */
#pragma once

#include "hw.h"

void calibStart();
bool calibUpdate(const Buttons &held, const Buttons &pressed);  // false = done
void calibDraw();
