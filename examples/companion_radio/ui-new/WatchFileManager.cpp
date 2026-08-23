#include "WatchFileManager.h"

#if defined(MECK_TWATCH) && defined(ESP32)

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <SPIFFS.h>
// CPU_FREQ_IDLE: the standalone envs define it (40) as a build flag; the
// BLE/WiFi envs leave it to this default, which mirrors CPUPowerManager.h.
// The header itself is not included here: its debug lines use MeshCore's
// MESH_DEBUG_PRINTLN macro, which is not in scope in this translation unit.
#ifndef CPU_FREQ_IDLE
#define CPU_FREQ_IDLE 80
#endif

// The two internal filesystems, addressed by a one-letter query parameter:
//   fs=l -> LittleFS ("maps" partition: steps.dat, notes, wifi.cfg, map tiles)
//   fs=s -> SPIFFS   (DataStore partition: prefs.json, contacts, channels,
//                     message history)
static fs::FS& wfmFS(char c) { return (c == 's') ? (fs::FS&)SPIFFS : (fs::FS&)LittleFS; }
static const char* wfmFSName(char c) { return (c == 's') ? "SPIFFS (DataStore)" : "LittleFS (maps)"; }

static WebServer* _wfm_srv = nullptr;
static bool       _wfm_active = false;
static char       _wfm_ssid[24] = "";
static File       _wfm_upload;

static char wfmArgFS() {
  String v = _wfm_srv->arg("fs");
  return (v.length() == 1 && v[0] == 's') ? 's' : 'l';
}

static void wfmHandleRoot() {
  String page;
  page.reserve(4096);
  page += "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width, initial-scale=1'>";
  page += "<title>Meck Watch File Manager</title><style>body{font-family:sans-serif;margin:12px}"
          "h2{margin:14px 0 4px}table{border-collapse:collapse}td{padding:2px 10px 2px 0}"
          "a{text-decoration:none}</style></head><body>";
  page += "<h1>Meck Watch File Manager</h1><p>http://192.168.4.1/ &mdash; files live on the "
          "watch's internal flash; there is no SD card.</p>";

  const char fss[2] = { 'l', 's' };
  for (int k = 0; k < 2; k++) {
    char c = fss[k];
    fs::FS& f = wfmFS(c);
    page += "<h2>";
    page += wfmFSName(c);
    page += "</h2><table>";
    File root = f.open("/");
    if (root && root.isDirectory()) {
      File e = root.openNextFile();
      while (e) {
        String nm = e.name();
        if (nm.length() && nm[0] != '/') nm = "/" + nm;
        if (e.isDirectory()) {
          // Directories (e.g. /tiles on the maps partition) are listed but not
          // expanded -- the tile tree can hold thousands of files.
          page += "<tr><td>[DIR] " + nm + "/</td><td></td><td></td></tr>";
        } else {
          page += "<tr><td>" + nm + "</td><td>" + String((unsigned long)e.size()) + " B</td>";
          page += "<td><a href='/dl?fs=" + String(c) + "&f=" + nm + "'>[download]</a> ";
          page += "<a href='/rm?fs=" + String(c) + "&f=" + nm +
                  "' onclick=\"return confirm('Delete " + nm + "?')\">[delete]</a></td></tr>";
        }
        e = root.openNextFile();
      }
    } else {
      page += "<tr><td>(not mounted)</td></tr>";
    }
    page += "</table>";
    page += "<form method='POST' action='/up?fs=" + String(c) +
            "' enctype='multipart/form-data'>Upload to " + String(wfmFSName(c)) +
            ": <input type='file' name='f'> <input type='submit' value='Upload'></form>";
  }

  page += "</body></html>";
  _wfm_srv->send(200, "text/html", page);
}

static void wfmHandleDownload() {
  String path = _wfm_srv->arg("f");
  fs::FS& f = wfmFS(wfmArgFS());
  if (path.length() == 0 || !f.exists(path)) { _wfm_srv->send(404, "text/plain", "not found"); return; }
  File file = f.open(path, "r");
  if (!file || file.isDirectory()) { _wfm_srv->send(404, "text/plain", "not found"); return; }
  String leaf = path.substring(path.lastIndexOf('/') + 1);
  _wfm_srv->sendHeader("Content-Disposition", "attachment; filename=\"" + leaf + "\"");
  _wfm_srv->streamFile(file, "application/octet-stream");
  file.close();
}

static void wfmHandleDelete() {
  String path = _wfm_srv->arg("f");
  fs::FS& f = wfmFS(wfmArgFS());
  if (path.length()) f.remove(path);
  _wfm_srv->sendHeader("Location", "/");
  _wfm_srv->send(303);
}

static void wfmHandleUploadData() {
  HTTPUpload& up = _wfm_srv->upload();
  fs::FS& f = wfmFS(wfmArgFS());
  if (up.status == UPLOAD_FILE_START) {
    String path = up.filename;
    if (path.length() == 0) return;
    if (path[0] != '/') path = "/" + path;
    _wfm_upload = f.open(path, "w");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (_wfm_upload) _wfm_upload.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END || up.status == UPLOAD_FILE_ABORTED) {
    if (_wfm_upload) _wfm_upload.close();
  }
}

bool wfmActive() { return _wfm_active; }
const char* wfmSSID() { return _wfm_ssid; }

void wfmStart() {
  if (_wfm_active) return;

  // WiFi will not run below 80 MHz; the standalone builds idle (and boost) at
  // 40. Run flat out while serving; wfmStop() returns to CPU_FREQ_IDLE.
  setCpuFrequencyMhz(240);

  uint64_t mac = ESP.getEfuseMac();
  snprintf(_wfm_ssid, sizeof(_wfm_ssid), "Meck-Watch-%04X", (unsigned)(mac & 0xFFFF));

  WiFi.mode(WIFI_AP);
  WiFi.softAP(_wfm_ssid);   // open network, same manner as the Meck OTA tools
  delay(100);

  _wfm_srv = new WebServer(80);
  _wfm_srv->on("/", HTTP_GET, wfmHandleRoot);
  _wfm_srv->on("/dl", HTTP_GET, wfmHandleDownload);
  _wfm_srv->on("/rm", HTTP_GET, wfmHandleDelete);
  _wfm_srv->on("/up", HTTP_POST, []() {
    _wfm_srv->sendHeader("Location", "/");
    _wfm_srv->send(303);
  }, wfmHandleUploadData);
  _wfm_srv->onNotFound([]() { _wfm_srv->send(404, "text/plain", "not found"); });
  _wfm_srv->begin();
  _wfm_active = true;

  Serial.printf("FileMgr: AP '%s' up, browse http://192.168.4.1/\n", _wfm_ssid);
}

void wfmStop() {
  if (!_wfm_active) return;
  _wfm_srv->stop();
  delete _wfm_srv;
  _wfm_srv = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  _wfm_active = false;
  setCpuFrequencyMhz(CPU_FREQ_IDLE);
  Serial.println("FileMgr: stopped");
}

void wfmLoop() {
  if (!_wfm_active) return;
  // The lock screen / boost-expiry paths can drop the clock under WiFi's
  // floor; re-assert before serving.
  if (getCpuFrequencyMhz() < 80) setCpuFrequencyMhz(240);
  _wfm_srv->handleClient();
}

#endif