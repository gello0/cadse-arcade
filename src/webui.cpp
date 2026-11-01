#include "webui.h"
#include "webpage.h"
#include "bno.h"
#include "onboardimu.h"
#include "imulog.h"
#include "net.h"
#include "scores.h"
#include "hw.h"

#include <WiFi.h>
#include <WebServer.h>
#include <SD_MMC.h>

static WebServer server(80);
static bool running = false;
static char ssid[16];

// only plain file names inside the log folder, nothing like ../
static bool validName(const String &f){
  if( f.length() == 0 || f.length() > 24 ) return false;
  for( size_t i = 0; i < f.length(); i++ ){
    char c = f[i];
    if( !(isalnum(c) || c == '_' || c == '-' || c == '.') ) return false;
  }
  return f.indexOf("..") < 0;
}

static void handleRoot(){
  server.send_P(200, "text/html", indexHtml);
}

static void handleLive(){
  char json[900];
  const ImuSample &b = bnoLatest();
  const OnboardSample &o = onboardLatest();
  snprintf(json, sizeof(json),
    "{\"t\":%lu,"
    "\"bno\":{\"ok\":%s,\"q\":[%.4f,%.4f,%.4f,%.4f],\"ypr\":[%.2f,%.2f,%.2f],"
    "\"lin\":[%.3f,%.3f,%.3f],\"gyr\":[%.4f,%.4f,%.4f],\"mag\":[%.2f,%.2f,%.2f],\"acc\":%d,\"hz\":%.0f},"
    "\"ob\":{\"ok\":%s,\"q\":[%.4f,%.4f,%.4f,%.4f],\"ypr\":[%.2f,%.2f,%.2f],"
    "\"acc\":[%.3f,%.3f,%.3f],\"gyr\":[%.4f,%.4f,%.4f],\"temp\":%.1f},"
    "\"log\":{\"on\":%s,\"file\":\"%s\",\"lines\":%lu,\"rate\":%d},"
    "\"sd\":%s,\"bat\":%d}",
    (unsigned long)millis(),
    bnoAvailable() ? "true" : "false", b.q[0], b.q[1], b.q[2], b.q[3], b.ypr[0], b.ypr[1], b.ypr[2],
    b.lin[0], b.lin[1], b.lin[2], b.gyr[0], b.gyr[1], b.gyr[2], b.mag[0], b.mag[1], b.mag[2],
    b.accuracy, bnoRateHz(),
    imuAvailable() ? "true" : "false", o.q[0], o.q[1], o.q[2], o.q[3], o.ypr[0], o.ypr[1], o.ypr[2],
    o.acc[0], o.acc[1], o.acc[2], o.gyr[0], o.gyr[1], o.gyr[2], o.tempC,
    imuLogActive() ? "true" : "false", imuLogFile(), (unsigned long)imuLogLines(), imuLogRate(),
    sdAvailable() ? "true" : "false", batteryPercent());
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

static void handleLogStart(){
  int rate = server.hasArg("rate") ? server.arg("rate").toInt() : 50;
  bool allowed = false;
  for( int i = 0; i < numImuLogRates; i++ ){
    if( imuLogRates[i] == rate ) allowed = true;
  }
  if( !allowed ){
    server.send(400, "text/plain", "rate must be 10, 25, 50 or 100");
    return;
  }
  if( !imuLogStart(rate) ){
    server.send(500, "text/plain", "no SD card");
    return;
  }
  server.send(200, "text/plain", imuLogFile());
}

static void handleLogStop(){
  imuLogStop();
  server.send(200, "text/plain", "stopped");
}

static void handleFiles(){
  String json = "{\"files\":[";
  if( sdAvailable() ){
    File dir = SD_MMC.open(imuLogDir);
    bool first = true;
    if( dir && dir.isDirectory() ){
      for( File f = dir.openNextFile(); f; f = dir.openNextFile() ){
        if( f.isDirectory() ) continue;
        String name = f.name();
        int slash = name.lastIndexOf('/');
        if( slash >= 0 ) name = name.substring(slash + 1);
        if( !first ) json += ",";
        json += "{\"name\":\"" + name + "\",\"size\":" + String((unsigned long)f.size()) + "}";
        first = false;
      }
    }
    uint64_t freeBytes = SD_MMC.totalBytes() - SD_MMC.usedBytes();
    json += "],\"freeMB\":" + String((unsigned long)(freeBytes / (1024 * 1024))) + "}";
  } else {
    json += "],\"freeMB\":0}";
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

static void handleDownload(){
  String f = server.arg("f");
  if( !validName(f) || !sdAvailable() ){
    server.send(400, "text/plain", "bad file name");
    return;
  }
  File file = SD_MMC.open(String(imuLogDir) + "/" + f, FILE_READ);
  if( !file ){
    server.send(404, "text/plain", "not found");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + f + "\"");
  server.streamFile(file, "text/csv");
  file.close();
}

static void handleDelete(){
  String f = server.arg("f");
  if( !validName(f) || !sdAvailable() ){
    server.send(400, "text/plain", "bad file name");
    return;
  }
  if( imuLogActive() && f == imuLogFile() ){
    server.send(409, "text/plain", "file is being recorded");
    return;
  }
  bool ok = SD_MMC.remove(String(imuLogDir) + "/" + f);
  server.send(ok ? 200 : 404, "text/plain", ok ? "deleted" : "not found");
}

void webStart(){
  if( running ){
    return;
  }
  // keep station mode alongside the access point so ESP-NOW still works
  WiFi.mode(WIFI_AP_STA);
  uint8_t mac[6];
  WiFi.macAddress(mac);
  uint16_t id = (mac[4] << 8) | mac[5];
  snprintf(ssid, sizeof(ssid), "CADSE-%04X", id);
  WiFi.softAP(ssid, nullptr, netChannel);   // open network, no password

  server.on("/", HTTP_GET, handleRoot);
  server.on("/live", HTTP_GET, handleLive);
  server.on("/log/start", HTTP_POST, handleLogStart);
  server.on("/log/stop", HTTP_POST, handleLogStop);
  server.on("/files", HTTP_GET, handleFiles);
  server.on("/download", HTTP_GET, handleDownload);
  server.on("/delete", HTTP_POST, handleDelete);
  server.onNotFound([](){ server.send(404, "text/plain", "not found"); });
  server.begin();
  running = true;
  Serial.printf("Web UI: join open network %s, open http://%s\n",
                ssid, WiFi.softAPIP().toString().c_str());
}

void webStop(){
  if( !running ){
    return;
  }
  server.stop();
  WiFi.softAPdisconnect(true);
  running = false;
}

void webService(){
  if( running ){
    server.handleClient();
  }
}

bool webRunning(){
  return running;
}

const char *webSsid(){
  return ssid;
}

int webClients(){
  return running ? WiFi.softAPgetStationNum() : 0;
}
