#include "net.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

static const uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

const int rxQueueSize = 16;

struct RxSlot {
  int len;
  uint8_t data[netMaxPacket];
};

static RxSlot rxQueue[rxQueueSize];
static volatile int rxHead = 0, rxTail = 0;
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

static bool started = false;
static bool ok = false;
static uint16_t myId = 0;

// runs in the Wi-Fi task: copy and get out
static void storePacket(const uint8_t *data, int len){
  if( len <= 0 || len > netMaxPacket ){
    return;
  }
  portENTER_CRITICAL(&rxMux);
  int next = (rxHead + 1) % rxQueueSize;
  if( next != rxTail ){        // drop the packet if the queue is full
    memcpy(rxQueue[rxHead].data, data, len);
    rxQueue[rxHead].len = len;
    rxHead = next;
  }
  portEXIT_CRITICAL(&rxMux);
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
static void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len){
  storePacket(data, len);
}
#else
static void onReceive(const uint8_t *mac, const uint8_t *data, int len){
  storePacket(data, len);
}
#endif

bool netBegin(){
  if( started ){
    return ok;
  }
  started = true;

  // keep the IMU logger's access point alive if it is running
  WiFi.mode((WiFi.getMode() & WIFI_AP) ? WIFI_AP_STA : WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(netChannel, WIFI_SECOND_CHAN_NONE);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  myId = (mac[4] << 8) | mac[5];

  if( esp_now_init() != ESP_OK ){
    Serial.println("ESP-NOW init failed");
    return false;
  }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMac, 6);
  peer.channel = netChannel;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  if( esp_now_add_peer(&peer) != ESP_OK ){
    Serial.println("ESP-NOW broadcast peer failed");
    return false;
  }

  Serial.printf("ESP-NOW ready, board id #%04X\n", myId);
  ok = true;
  return true;
}

uint16_t netId(){
  return myId;
}

bool netSend(const void *data, size_t len){
  if( !ok ){
    return false;
  }
  return esp_now_send(broadcastMac, (const uint8_t *)data, len) == ESP_OK;
}

bool netReceive(void *data, int &len){
  bool got = false;
  portENTER_CRITICAL(&rxMux);
  if( rxTail != rxHead ){
    len = rxQueue[rxTail].len;
    memcpy(data, rxQueue[rxTail].data, len);
    rxTail = (rxTail + 1) % rxQueueSize;
    got = true;
  }
  portEXIT_CRITICAL(&rxMux);
  return got;
}
