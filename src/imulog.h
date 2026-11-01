/* CSV logging of the BNO085 and the onboard IMU to the micro SD card
 *
 * Files go to /bno/log_NNN.csv. Lines are collected in RAM and written in
 * blocks so the SD card keeps up at 100 Hz.
 */
#pragma once

#include <Arduino.h>

const char *const imuLogDir = "/bno";
extern const int imuLogRates[];
extern const int numImuLogRates;

bool imuLogStart(int rateHz);     // false if there is no SD card
void imuLogStop();
void imuLogService();             // call as often as possible
bool imuLogActive();
int imuLogRate();
const char *imuLogFile();         // file name without the folder
uint32_t imuLogLines();
