/* Board-to-board radio link using ESP-NOW
 *
 * All boards talk on the same Wi-Fi channel by broadcast, no router needed.
 * Packets carry a sender and receiver id so many pairs can play in the same
 * room. Received packets are queued in the Wi-Fi task and read from loop().
 */
#pragma once

#include <Arduino.h>

const int netMaxPacket = 64;
const uint8_t netChannel = 1;

bool netBegin();                          // starts Wi-Fi + ESP-NOW once, safe to call again
uint16_t netId();                         // this board's id, the last two bytes of its MAC
bool netSend(const void *data, size_t len);
bool netReceive(void *data, int &len);    // false when the queue is empty
