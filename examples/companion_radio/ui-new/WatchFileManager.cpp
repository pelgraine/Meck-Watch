#include "WatchFileManager.h"

#if defined(MECK_TWATCH) && defined(ESP32)

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
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

// ---------------------------------------------------------------------------
// Watch File Manager -- a faithful copy of the upstream Meck OTA Tools' SD
// File Manager (SettingsScreen.h, MECK_OTA_UPDATE): captive-portal AP, one
// server-rendered page per folder (no JavaScript), 128-entry listing cap,
// streamed downloads, confirm-delete page, upload and new-folder forms, the
// same stylesheet. Differences from upstream, all deliberate:
//   - The watch has two internal filesystems and no SD, so the front page
//     ("/") shows two drives: /maps (LittleFS) and /datastore (SPIFFS).
//     Every deeper path routes to the matching filesystem.
//   - A "showing first 128" note when a folder overflows the cap -- the
//     DataStore root holds one file per contact, several hundred of them.
//   - New-folder is offered only under /maps: SPIFFS has no real folders.
//   - The LoRa radio keeps running (upstream pauses it because SD and LoRa
//     share an SPI bus there; the watch radio has its own bus).
// ---------------------------------------------------------------------------

static WebServer* _wfm_srv = nullptr;
static DNSServer* _wfm_dns = nullptr;
static bool       _wfm_active = false;
static char       _wfm_ssid[24] = "";
static File       _wfm_upload;

bool wfmActive() { return _wfm_active; }
const char* wfmSSID() { return _wfm_ssid; }

// --- Virtual path space: "/" is the drive list; "/maps/..." -> LittleFS,
// --- "/datastore/..." -> SPIFFS. Returns nullptr at the virtual root.
static fs::FS* wfmResolve(const String& vpath, String& real) {
  if (vpath.startsWith("/maps")) {
    real = vpath.substring(5);
    if (real.isEmpty()) real = "/";
    return (fs::FS*)&LittleFS;
  }
  if (vpath.startsWith("/datastore")) {
    real = vpath.substring(10);
    if (real.isEmpty()) real = "/";
    return (fs::FS*)&SPIFFS;
  }
  return nullptr;
}

// --- Helpers for server-rendered HTML (verbatim from upstream) ---

static String fmHtmlEscape(const String& s) {
  String r;
  r.reserve(s.length());
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') r += "&amp;";
    else if (c == '<') r += "&lt;";
    else if (c == '>') r += "&gt;";
    else if (c == '"') r += "&quot;";
    else r += c;
  }
  return r;
}

static String fmUrlEncode(const String& s) {
  String r;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '/' || c == '~') {
      r += c;
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", (uint8_t)c);
      r += hex;
    }
  }
  return r;
}

static String fmFormatSize(size_t bytes) {
  if (bytes < 1024) return String(bytes) + " B";
  if (bytes < 1048576) return String(bytes / 1024) + " KB";
  return String(bytes / 1048576) + "." + String((bytes % 1048576) * 10 / 1048576) + " MB";
}

// Build the complete HTML page with inline directory listing. Layout, CSS
// and behaviour copied from upstream Meck's fmBuildPage.
static String fmBuildPage(const String& path, const String& msg) {
  String html;
  html.reserve(4096);

  // --- Head + CSS ---
  html += "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Meck Watch Files</title>"
    "<style>"
    "body{font-family:-apple-system,sans-serif;max-width:600px;margin:20px auto;"
    "padding:0 16px;background:#1a1a2e;color:#e0e0e0}"
    "h1{color:#4ecca3;font-size:1.3em;margin:8px 0}"
    ".pa{background:#16213e;padding:8px 12px;border-radius:6px;margin:8px 0;"
    "font-family:monospace;font-size:0.9em;word-break:break-all}"
    ".tb{display:flex;gap:6px;margin:8px 0;flex-wrap:wrap}"
    ".b{background:#4ecca3;color:#1a1a2e;border:none;padding:7px 14px;"
    "border-radius:5px;font-size:0.85em;font-weight:bold;cursor:pointer;"
    "text-decoration:none;display:inline-block}"
    ".b:active{background:#3ba88f}"
    ".br{background:#e74c3c;color:#fff;padding:3px 8px;font-size:0.75em}.br:active{background:#c0392b}"
    ".it{display:flex;align-items:center;padding:8px 4px;border-bottom:1px solid #16213e;gap:6px}"
    ".ic{font-size:1.1em;width:22px;text-align:center}"
    ".nm{flex:1;word-break:break-all;color:#e0e0e0;text-decoration:none}"
    ".nm:hover{color:#4ecca3}"
    ".sz{color:#888;font-size:0.8em;min-width:54px;text-align:right;margin-right:4px}"
    ".up{background:#16213e;border:2px dashed #4ecca3;border-radius:8px;"
    "padding:14px;margin:10px 0;text-align:center}"
    ".em{color:#888;text-align:center;padding:20px}"
    ".ms{background:#16213e;padding:8px 12px;border-radius:6px;margin:8px 0;"
    "border-left:3px solid #4ecca3;font-size:0.9em}"
    "</style></head><body>";

  // --- Title + path ---
  html += "<h1>Meck File Manager</h1>";
  html += "<div class='pa'>" + fmHtmlEscape(path) + "</div>";

  // --- Status message (from redirects) ---
  if (msg.length() > 0) {
    html += "<div class='ms'>" + fmHtmlEscape(msg) + "</div>";
  }

  // --- Navigation buttons ---
  html += "<div class='tb'>";
  if (path != "/") {
    String parent = path;
    if (parent.endsWith("/")) parent = parent.substring(0, parent.length() - 1);
    int sl = parent.lastIndexOf('/');
    parent = (sl <= 0) ? "/" : parent.substring(0, sl);
    html += "<a class='b' href='/?path=" + fmUrlEncode(parent) + "'>.. Up</a>";
  }
  html += "<a class='b' href='/?path=" + fmUrlEncode(path) + "'>Refresh</a>";
  html += "</div>";

  String real;
  fs::FS* f = wfmResolve(path, real);

  if (f == nullptr) {
    // --- Virtual root: the two internal drives ---
    html += "<div class='it'><span class='ic'>\xF0\x9F\x93\x81</span>"
            "<a class='nm' href='/?path=/maps'>maps (LittleFS: steps, notes, wifi.cfg)</a></div>";
    html += "<div class='it'><span class='ic'>\xF0\x9F\x93\x81</span>"
            "<a class='nm' href='/?path=/datastore'>datastore (SPIFFS: prefs, contacts, channels)</a></div>";
  } else {
    // --- Directory listing (server-rendered) ---
    File dir = f->open(real, "r");
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      html += "<div class='em'>Cannot open directory</div>";
    } else {
      // Collect entries into arrays for sorting (dirs first, then alpha)
      struct FmEntry { String name; size_t size; bool isDir; };
      static FmEntry entries[128];  // max entries to display
      int count = 0;
      bool truncated = false;
      File entry = dir.openNextFile();
      while (entry) {
        if (count >= 128) { truncated = true; entry.close(); break; }
        const char* fullName = entry.name();
        const char* baseName = strrchr(fullName, '/');
        baseName = baseName ? baseName + 1 : fullName;
        entries[count].name = baseName;
        entries[count].size = entry.size();
        entries[count].isDir = entry.isDirectory();
        count++;
        entry.close();
        entry = dir.openNextFile();
      }
      dir.close();

      Serial.printf("FM: listing %d entries for '%s'\n", count, path.c_str());

      // Sort: dirs first, then alphabetical
      for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
          bool swap = false;
          if (entries[i].isDir != entries[j].isDir) {
            swap = !entries[i].isDir && entries[j].isDir;
          } else {
            swap = entries[i].name.compareTo(entries[j].name) > 0;
          }
          if (swap) {
            FmEntry tmp = entries[i];
            entries[i] = entries[j];
            entries[j] = tmp;
          }
        }
      }

      if (count == 0) {
        html += "<div class='em'>Empty folder</div>";
      } else {
        for (int i = 0; i < count; i++) {
          String fp = path + (path.endsWith("/") ? "" : "/") + entries[i].name;
          html += "<div class='it'>";
          html += "<span class='ic'>" + String(entries[i].isDir ? "\xF0\x9F\x93\x81" : "\xF0\x9F\x93\x84") + "</span>";
          if (entries[i].isDir) {
            html += "<a class='nm' href='/?path=" + fmUrlEncode(fp) + "'>" + fmHtmlEscape(entries[i].name) + "</a>";
          } else {
            html += "<a class='nm' href='/dl?path=" + fmUrlEncode(fp) + "'>" + fmHtmlEscape(entries[i].name) + "</a>";
            html += "<span class='sz'>" + fmFormatSize(entries[i].size) + "</span>";
          }
          html += "<a class='b br' href='/confirm-rm?path=" + fmUrlEncode(fp) + "&ret=" + fmUrlEncode(path) + "'>Del</a>";
          html += "</div>";
        }
        if (truncated) {
          html += "<div class='em'>Showing the first 128 entries only</div>";
        }
      }
    }

    // --- Upload form (standard HTML form, no JS needed) ---
    html += "<div class='up'>"
      "<form method='POST' action='/upload?dir=" + fmUrlEncode(path) + "' enctype='multipart/form-data'>"
      "<p>Select files to upload</p>"
      "<input type='file' name='file' multiple><br><br>"
      "<button class='b' type='submit'>Upload</button>"
      "</form></div>";

    // --- New folder (tiny inline form) -- LittleFS only; SPIFFS is flat ---
    if (path.startsWith("/maps")) {
      html += "<form action='/mkdir' method='GET' style='margin:8px 0;display:flex;gap:6px'>"
        "<input type='hidden' name='dir' value='" + fmHtmlEscape(path) + "'>"
        "<input type='text' name='name' placeholder='New folder name' "
        "style='flex:1;padding:7px;border-radius:5px;border:1px solid #4ecca3;"
        "background:#16213e;color:#e0e0e0'>"
        "<button class='b' type='submit'>Create</button>"
        "</form>";
    }
  }

  html += "</body></html>";
  return html;
}

void wfmStart() {
  if (_wfm_active) return;

  // WiFi will not run below 80 MHz; the standalone builds idle (and boost) at
  // 40. Run flat out while serving; wfmStop() returns to CPU_FREQ_IDLE.
  setCpuFrequencyMhz(240);

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(_wfm_ssid, sizeof(_wfm_ssid), "Meck-Files-%02X%02X", mac[4], mac[5]);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(_wfm_ssid);   // open network, same manner as the Meck OTA tools
  delay(500);
  Serial.printf("FM: AP '%s' started, IP: %s\n",
                _wfm_ssid, WiFi.softAPIP().toString().c_str());

  // DNS server: redirect ALL lookups to our AP IP. This triggers captive
  // portal detection on phones, which opens the page in a real browser.
  if (_wfm_dns) { delete _wfm_dns; }
  _wfm_dns = new DNSServer();
  _wfm_dns->start(53, "*", WiFi.softAPIP());
  Serial.println("FM: DNS captive portal started");

  if (_wfm_srv) { _wfm_srv->stop(); delete _wfm_srv; }
  _wfm_srv = new WebServer(80);

  // --- Captive portal detection handlers (Apple / Android / Windows / Firefox) ---
  auto captive = []() {
    _wfm_srv->sendHeader("Location", "http://192.168.4.1/");
    _wfm_srv->send(302, "text/plain", "");
  };
  _wfm_srv->on("/hotspot-detect.html", HTTP_GET, captive);
  _wfm_srv->on("/generate_204", HTTP_GET, captive);
  _wfm_srv->on("/gen_204", HTTP_GET, captive);
  _wfm_srv->on("/connecttest.txt", HTTP_GET, captive);
  _wfm_srv->on("/redirect", HTTP_GET, captive);
  _wfm_srv->on("/canonical.html", HTTP_GET, captive);
  _wfm_srv->on("/success.txt", HTTP_GET, []() {
    _wfm_srv->send(200, "text/plain", "success");
  });

  // --- Main page: server-rendered directory listing (no JS needed) ---
  _wfm_srv->on("/", HTTP_GET, []() {
    String path = _wfm_srv->arg("path");
    if (path.isEmpty()) path = "/";
    String msg = _wfm_srv->arg("msg");
    Serial.printf("FM: page request path='%s'\n", path.c_str());
    String html = fmBuildPage(path, msg);
    _wfm_srv->send(200, "text/html", html);
  });

  // --- File download: GET /dl?path=/maps/steps.dat ---
  _wfm_srv->on("/dl", HTTP_GET, []() {
    String path = _wfm_srv->arg("path");
    String real;
    fs::FS* fsp = wfmResolve(path, real);
    File f = fsp ? fsp->open(real, "r") : File();
    if (!f || f.isDirectory()) {
      if (f) f.close();
      _wfm_srv->send(404, "text/plain", "Not found");
      return;
    }
    String name = path;
    int lastSlash = name.lastIndexOf('/');
    if (lastSlash >= 0) name = name.substring(lastSlash + 1);
    _wfm_srv->sendHeader("Content-Disposition",
      "attachment; filename=\"" + name + "\"");
    size_t fileSize = f.size();
    _wfm_srv->setContentLength(fileSize);
    _wfm_srv->send(200, "application/octet-stream", "");
    uint8_t* buf = (uint8_t*)ps_malloc(4096);
    if (!buf) buf = (uint8_t*)malloc(4096);
    if (buf) {
      while (f.available()) {
        int n = f.read(buf, 4096);
        if (n > 0) _wfm_srv->sendContent((const char*)buf, n);
      }
      free(buf);
    }
    f.close();
  });

  // --- File upload: POST /upload?dir=/maps -> redirect back to listing ---
  _wfm_srv->on("/upload", HTTP_POST,
    []() {
      String dir = _wfm_srv->arg("dir");
      if (dir.isEmpty()) dir = "/";
      _wfm_srv->sendHeader("Location", "/?path=" + fmUrlEncode(dir) + "&msg=Upload+complete");
      _wfm_srv->send(303, "text/plain", "Redirecting...");
    },
    []() {
      HTTPUpload& upload = _wfm_srv->upload();
      if (upload.status == UPLOAD_FILE_START) {
        String dir = _wfm_srv->arg("dir");
        if (dir.isEmpty()) dir = "/";
        if (!dir.endsWith("/")) dir += "/";
        String real;
        fs::FS* fsp = wfmResolve(dir + upload.filename, real);
        Serial.printf("FM: Upload start: %s%s\n", dir.c_str(), upload.filename.c_str());
        _wfm_upload = fsp ? fsp->open(real, "w") : File();
        if (!_wfm_upload) Serial.println("FM: Failed to open file for write");
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (_wfm_upload) _wfm_upload.write(upload.buf, upload.currentSize);
      } else if (upload.status == UPLOAD_FILE_END) {
        if (_wfm_upload) {
          _wfm_upload.close();
          Serial.printf("FM: Upload done: %s (%d bytes)\n",
                        upload.filename.c_str(), upload.totalSize);
        }
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        if (_wfm_upload) _wfm_upload.close();
        Serial.println("FM: Upload aborted");
      }
    }
  );

  // --- Create directory: GET /mkdir?name=xxx&dir=/maps -- LittleFS only ---
  _wfm_srv->on("/mkdir", HTTP_GET, []() {
    String dir = _wfm_srv->arg("dir");
    String name = _wfm_srv->arg("name");
    if (dir.isEmpty()) dir = "/";
    if (name.isEmpty()) {
      _wfm_srv->sendHeader("Location", "/?path=" + fmUrlEncode(dir) + "&msg=No+name");
      _wfm_srv->send(303);
      return;
    }
    String full = dir + (dir.endsWith("/") ? "" : "/") + name;
    String real;
    fs::FS* fsp = wfmResolve(full, real);
    bool ok = (fsp == (fs::FS*)&LittleFS) && fsp->mkdir(real);
    Serial.printf("FM: mkdir '%s' %s\n", full.c_str(), ok ? "OK" : "FAIL");
    _wfm_srv->sendHeader("Location",
      "/?path=" + fmUrlEncode(dir) + "&msg=" + (ok ? "Folder+created" : "mkdir+failed"));
    _wfm_srv->send(303);
  });

  // --- Delete file/folder: GET /rm?path=/maps/x&ret=/maps ---
  _wfm_srv->on("/rm", HTTP_GET, []() {
    String path = _wfm_srv->arg("path");
    String ret = _wfm_srv->arg("ret");
    if (ret.isEmpty()) ret = "/";
    String real;
    fs::FS* fsp = wfmResolve(path, real);
    if (fsp == nullptr || real == "/" || path.isEmpty()) {
      _wfm_srv->sendHeader("Location", "/?path=" + fmUrlEncode(ret) + "&msg=Bad+path");
      _wfm_srv->send(303);
      return;
    }
    File f = fsp->open(real, "r");
    bool ok = false;
    if (f) {
      bool isDir = f.isDirectory();
      f.close();
      ok = isDir ? fsp->rmdir(real) : fsp->remove(real);
    }
    Serial.printf("FM: rm '%s' %s\n", path.c_str(), ok ? "OK" : "FAIL");
    _wfm_srv->sendHeader("Location",
      "/?path=" + fmUrlEncode(ret) + "&msg=" + (ok ? "Deleted" : "Delete+failed"));
    _wfm_srv->send(303);
  });

  // --- Confirm delete page: GET /confirm-rm?path=/maps/x&ret=/maps ---
  _wfm_srv->on("/confirm-rm", HTTP_GET, []() {
    String path = _wfm_srv->arg("path");
    String ret = _wfm_srv->arg("ret");
    if (ret.isEmpty()) ret = "/";
    String name = path;
    int sl = name.lastIndexOf('/');
    if (sl >= 0) name = name.substring(sl + 1);
    String html = "<!DOCTYPE html><html><head>"
      "<meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Confirm Delete</title>"
      "<style>"
      "body{font-family:-apple-system,sans-serif;max-width:480px;margin:40px auto;"
      "padding:0 20px;background:#1a1a2e;color:#e0e0e0;text-align:center}"
      ".b{display:inline-block;padding:10px 24px;border-radius:6px;text-decoration:none;"
      "font-weight:bold;margin:8px;font-size:1em}"
      ".br{background:#e74c3c;color:#fff}.bg{background:#4ecca3;color:#1a1a2e}"
      "</style></head><body>"
      "<h2 style='color:#e74c3c'>Delete?</h2>"
      "<p style='font-size:1.1em'>" + fmHtmlEscape(name) + "</p>"
      "<a class='b br' href='/rm?path=" + fmUrlEncode(path) + "&ret=" + fmUrlEncode(ret) + "'>Delete</a>"
      "<a class='b bg' href='/?path=" + fmUrlEncode(ret) + "'>Cancel</a>"
      "</body></html>";
    _wfm_srv->send(200, "text/html", html);
  });

  // Catch-all: redirect unknown URLs to the file manager (captive probes)
  _wfm_srv->onNotFound([]() {
    _wfm_srv->sendHeader("Location", "http://192.168.4.1/");
    _wfm_srv->send(302, "text/plain", "");
  });

  _wfm_srv->begin();
  _wfm_active = true;
  Serial.println("FM: Web server started on port 80");
}

void wfmStop() {
  if (!_wfm_active) return;
  if (_wfm_srv) { _wfm_srv->stop(); delete _wfm_srv; _wfm_srv = nullptr; }
  if (_wfm_dns) { _wfm_dns->stop(); delete _wfm_dns; _wfm_dns = nullptr; }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  _wfm_active = false;
  setCpuFrequencyMhz(CPU_FREQ_IDLE);
  Serial.println("FM: Stopped, AP down");
}

void wfmLoop() {
  if (!_wfm_active) return;
  // The lock screen / boost-expiry paths can drop the clock under WiFi's
  // floor; re-assert before serving.
  if (getCpuFrequencyMhz() < 80) setCpuFrequencyMhz(240);
  _wfm_dns->processNextRequest();
  _wfm_srv->handleClient();
}

#endif