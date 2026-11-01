/* SYNTH app: play the buzzer with the touch pads and by tilting the board */
#pragma once

#include "hw.h"

void synthStart();
bool synthUpdate(const Buttons &held, const Buttons &pressed);  // false = back to menu
void synthDraw();
