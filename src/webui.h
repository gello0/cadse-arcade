/* Wi-Fi access point and web server for the IMU logger
 *
 * The board opens its own open network "CADSE-XXXX" (XXXX = board id, no
 * password, so anyone in range can join), the page lives at
 * http://192.168.4.1. No internet needed.
 *
 *   GET  /             live page
 *   GET  /live         latest BNO085 + onboard IMU readings as JSON
 *   POST /log/start    ?rate=10|25|50|100
 *   POST /log/stop
 *   GET  /files        log files on the SD card as JSON
 *   GET  /download     ?f=log_001.csv
 *   POST /delete       ?f=log_001.csv
 */
#pragma once

#include <Arduino.h>

void webStart();
void webStop();
void webService();               // call as often as possible
bool webRunning();
const char *webSsid();
int webClients();                // devices connected to the access point
