#include <Arduino.h>
#include "DataStore.h"

#if defined(EXTRAFS) || defined(QSPIFLASH)
  #define MAX_BLOBRECS 100
#else
  #define MAX_BLOBRECS 20
#endif

DataStore::DataStore(FILESYSTEM& fs, mesh::RTCClock& clock) : _fs(&fs), _fsExtra(nullptr), _clock(&clock),
    identity_store(fs, "/identity")
{
}

#if defined(EXTRAFS) || defined(QSPIFLASH)
DataStore::DataStore(FILESYSTEM& fs, FILESYSTEM& fsExtra, mesh::RTCClock& clock) : _fs(&fs), _fsExtra(&fsExtra), _clock(&clock),
    identity_store(fs, "/identity")
{
}
#endif

static File openWrite(FILESYSTEM* fs, const char* filename) {
  return fs->open(filename, "w", true);
}


void DataStore::begin() {

  // init 'blob store' support
  _fs->mkdir("/bl");
}

#if defined(ESP32)
  #include <SPIFFS.h>
  #include <nvs_flash.h>
#endif


uint32_t DataStore::getStorageUsedKb() const {
#if defined(ESP32)
  return SPIFFS.usedBytes() / 1024;
#else
  return 0;
#endif
}

uint32_t DataStore::getStorageTotalKb() const {
#if defined(ESP32)
  return SPIFFS.totalBytes() / 1024;
#else
  return 0;
#endif
}

File DataStore::openRead(const char* filename) {
  return _fs->open(filename, "r", false);
}

File DataStore::openRead(FILESYSTEM* fs, const char* filename) {
  return fs->open(filename, "r", false);
}

bool DataStore::removeFile(const char* filename) {
  return _fs->remove(filename);
}

bool DataStore::removeFile(FILESYSTEM* fs, const char* filename) {
  return fs->remove(filename);
}

bool DataStore::formatFileSystem() {
#if   defined(ESP32)
  bool fs_success = ((fs::SPIFFSFS *)_fs)->format();
  esp_err_t nvs_err = nvs_flash_erase(); // no need to reinit, will be done by reboot
  return fs_success && (nvs_err == ESP_OK);
#else
  #error "need to implement format()"
#endif
}

bool DataStore::loadMainIdentity(mesh::LocalIdentity &identity) {
  return identity_store.load("_main", identity);
}

bool DataStore::saveMainIdentity(const mesh::LocalIdentity &identity) {
  return identity_store.save("_main", identity);
}

void DataStore::loadPrefs(NodePrefs& prefs, double& node_lat, double& node_lon) {
  if (_fs->exists("/new_prefs")) {
    loadPrefsInt("/new_prefs", prefs, node_lat, node_lon); // new filename
  } else if (_fs->exists("/node_prefs")) {
    loadPrefsInt("/node_prefs", prefs, node_lat, node_lon);
    savePrefs(prefs, node_lat, node_lon);                // save to new filename
    _fs->remove("/node_prefs"); // remove old
  }
  // Fresh device (no prefs file) leaves backlight at memset 0; treat any
  // out-of-range value as the default so the heart button has a sane level.
  if (prefs.backlight_brightness_pct < 5 || prefs.backlight_brightness_pct > 100) {
    prefs.backlight_brightness_pct = 100;
  }
  // Same treatment for the keyboard LED level (fresh device = memset 0).
  if (prefs.kb_backlight_pct < 5 || prefs.kb_backlight_pct > 100) {
    prefs.kb_backlight_pct = 50;
  }
}

void DataStore::loadPrefsInt(const char *filename, NodePrefs& _prefs, double& node_lat, double& node_lon) {
  File file = openRead(_fs, filename);
  if (file) {
    uint8_t pad[8];

    file.read((uint8_t *)&_prefs.airtime_factor, sizeof(float));                           // 0
    file.read((uint8_t *)_prefs.node_name, sizeof(_prefs.node_name));                      // 4
    file.read(pad, 4);                                                                     // 36
    file.read((uint8_t *)&node_lat, sizeof(node_lat));                                     // 40
    file.read((uint8_t *)&node_lon, sizeof(node_lon));                                     // 48
    file.read((uint8_t *)&_prefs.freq, sizeof(_prefs.freq));                               // 56
    file.read((uint8_t *)&_prefs.sf, sizeof(_prefs.sf));                                   // 60
    file.read((uint8_t *)&_prefs.cr, sizeof(_prefs.cr));                                   // 61
    file.read(pad, 1);                                                                     // 62
    file.read((uint8_t *)&_prefs.manual_add_contacts, sizeof(_prefs.manual_add_contacts)); // 63
    file.read((uint8_t *)&_prefs.bw, sizeof(_prefs.bw));                                   // 64
    file.read((uint8_t *)&_prefs.tx_power_dbm, sizeof(_prefs.tx_power_dbm));               // 68
    file.read((uint8_t *)&_prefs.telemetry_mode_base, sizeof(_prefs.telemetry_mode_base)); // 69
    file.read((uint8_t *)&_prefs.telemetry_mode_loc, sizeof(_prefs.telemetry_mode_loc));   // 70
    file.read((uint8_t *)&_prefs.telemetry_mode_env, sizeof(_prefs.telemetry_mode_env));   // 71
    file.read((uint8_t *)&_prefs.rx_delay_base, sizeof(_prefs.rx_delay_base));             // 72
    file.read((uint8_t *)&_prefs.advert_loc_policy, sizeof(_prefs.advert_loc_policy));     // 76
    file.read((uint8_t *)&_prefs.multi_acks, sizeof(_prefs.multi_acks));                   // 77
    file.read(pad, 2);                                                                     // 78
    file.read((uint8_t *)&_prefs.ble_pin, sizeof(_prefs.ble_pin));                         // 80
    file.read((uint8_t *)&_prefs.buzzer_quiet, sizeof(_prefs.buzzer_quiet));               // 84
    file.read((uint8_t *)&_prefs.gps_enabled, sizeof(_prefs.gps_enabled));                 // 85
    file.read((uint8_t *)&_prefs.gps_interval, sizeof(_prefs.gps_interval));               // 86
    file.read((uint8_t *)&_prefs.autoadd_config, sizeof(_prefs.autoadd_config));           // 87
    file.read((uint8_t *)&_prefs.utc_offset_hours, sizeof(_prefs.utc_offset_hours));       // 88

    // Fields added later — may not exist in older prefs files
    if (file.read((uint8_t *)&_prefs.kb_flash_notify, sizeof(_prefs.kb_flash_notify)) != sizeof(_prefs.kb_flash_notify)) {
      _prefs.kb_flash_notify = 0;  // default OFF for old files
    }
    if (file.read((uint8_t *)&_prefs.ringtone_enabled, sizeof(_prefs.ringtone_enabled)) != sizeof(_prefs.ringtone_enabled)) {
      _prefs.ringtone_enabled = 0;  // default OFF for old files
    }

    // Clamp booleans to 0/1 in case of garbage
    if (_prefs.kb_flash_notify > 1) _prefs.kb_flash_notify = 0;
    if (_prefs.ringtone_enabled > 1) _prefs.ringtone_enabled = 0;

    // v1.14+ fields — may not exist in older prefs files
    if (file.read((uint8_t *)&_prefs.path_hash_mode, sizeof(_prefs.path_hash_mode)) != sizeof(_prefs.path_hash_mode)) {
      _prefs.path_hash_mode = 0;  // default: legacy 1-byte
    }
    if (file.read((uint8_t *)&_prefs.autoadd_max_hops, sizeof(_prefs.autoadd_max_hops)) != sizeof(_prefs.autoadd_max_hops)) {
      _prefs.autoadd_max_hops = 0;  // default: no limit
    }
    if (_prefs.path_hash_mode > 2) _prefs.path_hash_mode = 0;
    if (_prefs.autoadd_max_hops > 64) _prefs.autoadd_max_hops = 0;

    // v1.1+ Meck fields — may not exist in older prefs files
    if (file.read((uint8_t *)&_prefs.gps_baudrate, sizeof(_prefs.gps_baudrate)) != sizeof(_prefs.gps_baudrate)) {
      _prefs.gps_baudrate = 0;  // default: use compile-time GPS_BAUDRATE
    }
    if (file.read((uint8_t *)&_prefs.interference_threshold, sizeof(_prefs.interference_threshold)) != sizeof(_prefs.interference_threshold)) {
      _prefs.interference_threshold = 0;  // default: disabled
    }
    if (file.read((uint8_t *)&_prefs.dark_mode, sizeof(_prefs.dark_mode)) != sizeof(_prefs.dark_mode)) {
      _prefs.dark_mode = 0;  // default: light mode
    }
    if (file.read((uint8_t *)&_prefs.portrait_mode, sizeof(_prefs.portrait_mode)) != sizeof(_prefs.portrait_mode)) {
      _prefs.portrait_mode = 0;  // default: landscape
    }
    if (file.read((uint8_t *)&_prefs.auto_lock_minutes, sizeof(_prefs.auto_lock_minutes)) != sizeof(_prefs.auto_lock_minutes)) {
      _prefs.auto_lock_minutes = 0;  // default: disabled
    }
    if (file.read((uint8_t *)&_prefs.hint_shown, sizeof(_prefs.hint_shown)) != sizeof(_prefs.hint_shown)) {
      _prefs.hint_shown = 0;  // default: show boot hint
    }
    if (file.read((uint8_t *)&_prefs.large_font, sizeof(_prefs.large_font)) != sizeof(_prefs.large_font)) {
      _prefs.large_font = 0;  // default: tiny font
    }
    if (file.read((uint8_t *)&_prefs.tx_fail_reset_threshold, sizeof(_prefs.tx_fail_reset_threshold)) != sizeof(_prefs.tx_fail_reset_threshold)) {
      _prefs.tx_fail_reset_threshold = 3;  // default: 3
    }
    if (file.read((uint8_t *)&_prefs.rx_fail_reboot_threshold, sizeof(_prefs.rx_fail_reboot_threshold)) != sizeof(_prefs.rx_fail_reboot_threshold)) {
      _prefs.rx_fail_reboot_threshold = 3;  // default: 3
    }
    if (file.read((uint8_t *)&_prefs.ui_font_style, sizeof(_prefs.ui_font_style)) != sizeof(_prefs.ui_font_style)) {
      _prefs.ui_font_style = 0;  // default: Classic (FreeSans)
    }
    if (file.read((uint8_t *)_prefs.default_scope_name, sizeof(_prefs.default_scope_name)) != sizeof(_prefs.default_scope_name)) {
      memset(_prefs.default_scope_name, 0, sizeof(_prefs.default_scope_name));
    }
    if (file.read((uint8_t *)_prefs.default_scope_key, sizeof(_prefs.default_scope_key)) != sizeof(_prefs.default_scope_key)) {
      memset(_prefs.default_scope_key, 0, sizeof(_prefs.default_scope_key));
    }
    if (file.read((uint8_t *)_prefs.channel_notif, sizeof(_prefs.channel_notif)) != sizeof(_prefs.channel_notif)) {
      memset(_prefs.channel_notif, 0, sizeof(_prefs.channel_notif));  // default: NOTIF_ALL
    }
    if (file.read((uint8_t *)&_prefs.lora_antenna, sizeof(_prefs.lora_antenna)) != sizeof(_prefs.lora_antenna)) {
      _prefs.lora_antenna = 0;  // default: internal antenna
    }
    if (file.read((uint8_t *)&_prefs.backlight_brightness_pct, sizeof(_prefs.backlight_brightness_pct)) != sizeof(_prefs.backlight_brightness_pct)) {
      _prefs.backlight_brightness_pct = 100;  // default: full brightness
    }
    if (file.read((uint8_t *)&_prefs.kb_backlight_pct, sizeof(_prefs.kb_backlight_pct)) != sizeof(_prefs.kb_backlight_pct)) {
      _prefs.kb_backlight_pct = 50;  // default: 50%
    }
    if (file.read((uint8_t *)&_prefs.rx_duty_cycle, sizeof(_prefs.rx_duty_cycle)) != sizeof(_prefs.rx_duty_cycle)) {
      _prefs.rx_duty_cycle = 0;  // default: off
    }
    if (file.read((uint8_t *)&_prefs.rx_duty_level, sizeof(_prefs.rx_duty_level)) != sizeof(_prefs.rx_duty_level)) {
      _prefs.rx_duty_level = 0;  // default: Light
    }

    // Clamp to valid ranges
    if (_prefs.dark_mode > 1) _prefs.dark_mode = 0;
    if (_prefs.portrait_mode > 1) _prefs.portrait_mode = 0;
    if (_prefs.hint_shown > 1) _prefs.hint_shown = 0;
    if (_prefs.large_font > 1) _prefs.large_font = 0;
    if (_prefs.ui_font_style > 2) _prefs.ui_font_style = 0;
    if (_prefs.tx_fail_reset_threshold > 10) _prefs.tx_fail_reset_threshold = 3;
    if (_prefs.rx_fail_reboot_threshold > 10) _prefs.rx_fail_reboot_threshold = 3;
    // Clamp channel notification preferences to valid range
    for (int i = 0; i < (int)sizeof(_prefs.channel_notif); i++) {
      if (_prefs.channel_notif[i] > 2) _prefs.channel_notif[i] = 0;
    }
    if (_prefs.lora_antenna > 1) _prefs.lora_antenna = 0;
    if (_prefs.backlight_brightness_pct < 5 || _prefs.backlight_brightness_pct > 100) _prefs.backlight_brightness_pct = 100;
    if (_prefs.kb_backlight_pct < 5 || _prefs.kb_backlight_pct > 100) _prefs.kb_backlight_pct = 50;
    if (_prefs.rx_duty_cycle > 1) _prefs.rx_duty_cycle = 0;
    if (_prefs.rx_duty_level > 2) _prefs.rx_duty_level = 0;
    // auto_lock_minutes: only accept known options (0, 2, 5, 10, 15, 30)
    {
      uint8_t alm = _prefs.auto_lock_minutes;
      if (alm != 0 && alm != 2 && alm != 5 && alm != 10 && alm != 15 && alm != 30) {
        _prefs.auto_lock_minutes = 0;
      }
    }

    file.close();
  }
}

void DataStore::savePrefs(const NodePrefs& _prefs, double node_lat, double node_lon) {
  File file = openWrite(_fs, "/new_prefs");
  if (file) {
    uint8_t pad[8];
    memset(pad, 0, sizeof(pad));

    file.write((uint8_t *)&_prefs.airtime_factor, sizeof(float));                           // 0
    file.write((uint8_t *)_prefs.node_name, sizeof(_prefs.node_name));                      // 4
    file.write(pad, 4);                                                                     // 36
    file.write((uint8_t *)&node_lat, sizeof(node_lat));                                     // 40
    file.write((uint8_t *)&node_lon, sizeof(node_lon));                                     // 48
    file.write((uint8_t *)&_prefs.freq, sizeof(_prefs.freq));                               // 56
    file.write((uint8_t *)&_prefs.sf, sizeof(_prefs.sf));                                   // 60
    file.write((uint8_t *)&_prefs.cr, sizeof(_prefs.cr));                                   // 61
    file.write(pad, 1);                                                                     // 62
    file.write((uint8_t *)&_prefs.manual_add_contacts, sizeof(_prefs.manual_add_contacts)); // 63
    file.write((uint8_t *)&_prefs.bw, sizeof(_prefs.bw));                                   // 64
    file.write((uint8_t *)&_prefs.tx_power_dbm, sizeof(_prefs.tx_power_dbm));               // 68
    file.write((uint8_t *)&_prefs.telemetry_mode_base, sizeof(_prefs.telemetry_mode_base)); // 69
    file.write((uint8_t *)&_prefs.telemetry_mode_loc, sizeof(_prefs.telemetry_mode_loc));   // 70
    file.write((uint8_t *)&_prefs.telemetry_mode_env, sizeof(_prefs.telemetry_mode_env));   // 71
    file.write((uint8_t *)&_prefs.rx_delay_base, sizeof(_prefs.rx_delay_base));             // 72
    file.write((uint8_t *)&_prefs.advert_loc_policy, sizeof(_prefs.advert_loc_policy));     // 76
    file.write((uint8_t *)&_prefs.multi_acks, sizeof(_prefs.multi_acks));                   // 77
    file.write(pad, 2);                                                                     // 78
    file.write((uint8_t *)&_prefs.ble_pin, sizeof(_prefs.ble_pin));                         // 80
    file.write((uint8_t *)&_prefs.buzzer_quiet, sizeof(_prefs.buzzer_quiet));               // 84
    file.write((uint8_t *)&_prefs.gps_enabled, sizeof(_prefs.gps_enabled));                 // 85
    file.write((uint8_t *)&_prefs.gps_interval, sizeof(_prefs.gps_interval));               // 86
    file.write((uint8_t *)&_prefs.autoadd_config, sizeof(_prefs.autoadd_config));           // 87
    file.write((uint8_t *)&_prefs.utc_offset_hours, sizeof(_prefs.utc_offset_hours));     // 88
    file.write((uint8_t *)&_prefs.kb_flash_notify, sizeof(_prefs.kb_flash_notify));      // 89
    file.write((uint8_t *)&_prefs.ringtone_enabled, sizeof(_prefs.ringtone_enabled));    // 90
    file.write((uint8_t *)&_prefs.path_hash_mode, sizeof(_prefs.path_hash_mode));        // 91
    file.write((uint8_t *)&_prefs.autoadd_max_hops, sizeof(_prefs.autoadd_max_hops));   // 92
    file.write((uint8_t *)&_prefs.gps_baudrate, sizeof(_prefs.gps_baudrate));            // 93
    file.write((uint8_t *)&_prefs.interference_threshold, sizeof(_prefs.interference_threshold)); // 97
    file.write((uint8_t *)&_prefs.dark_mode, sizeof(_prefs.dark_mode));                  // 98
    file.write((uint8_t *)&_prefs.portrait_mode, sizeof(_prefs.portrait_mode));          // 99
    file.write((uint8_t *)&_prefs.auto_lock_minutes, sizeof(_prefs.auto_lock_minutes)); // 100
    file.write((uint8_t *)&_prefs.hint_shown, sizeof(_prefs.hint_shown));               // 101
    file.write((uint8_t *)&_prefs.large_font, sizeof(_prefs.large_font));               // 102
    file.write((uint8_t *)&_prefs.tx_fail_reset_threshold, sizeof(_prefs.tx_fail_reset_threshold)); // 103
    file.write((uint8_t *)&_prefs.rx_fail_reboot_threshold, sizeof(_prefs.rx_fail_reboot_threshold)); // 104
    file.write((uint8_t *)&_prefs.ui_font_style, sizeof(_prefs.ui_font_style));                 // 105
    file.write((uint8_t *)_prefs.default_scope_name, sizeof(_prefs.default_scope_name));         // 106
    file.write((uint8_t *)_prefs.default_scope_key, sizeof(_prefs.default_scope_key));           // 137
    file.write((uint8_t *)_prefs.channel_notif, sizeof(_prefs.channel_notif));                   // 153
    file.write((uint8_t *)&_prefs.lora_antenna, sizeof(_prefs.lora_antenna));                    // 174
    file.write((uint8_t *)&_prefs.backlight_brightness_pct, sizeof(_prefs.backlight_brightness_pct)); // 175
    file.write((uint8_t *)&_prefs.kb_backlight_pct, sizeof(_prefs.kb_backlight_pct));            // 176
    file.write((uint8_t *)&_prefs.rx_duty_cycle, sizeof(_prefs.rx_duty_cycle));                  // 177
    file.write((uint8_t *)&_prefs.rx_duty_level, sizeof(_prefs.rx_duty_level));                  // 178

    file.close();
  }
}

void DataStore::loadContacts(DataStoreHost* host) {
  FILESYSTEM* fs = _getContactsChannelsFS();

  // --- Crash recovery ---
  // If /contacts3 is missing but /contacts3.tmp exists, a crash occurred
  // after removing the original but before the rename completed.
  // The .tmp file has the valid data — promote it.
  if (!fs->exists("/contacts3") && fs->exists("/contacts3.tmp")) {
    Serial.println("DataStore: recovering contacts from .tmp file");
    fs->rename("/contacts3.tmp", "/contacts3");
  }
  // If both exist, a crash occurred before the old file was removed.
  // The original /contacts3 is still valid — just clean up the orphan.
  if (fs->exists("/contacts3.tmp")) {
    fs->remove("/contacts3.tmp");
  }

  File file = openRead(fs, "/contacts3");
    if (file) {
      // --- Truncation guard ---
      // If the file is smaller than one full contact record (152 bytes),
      // it was truncated by a crash/brown-out. Discard it and try the
      // .tmp backup if available.
      size_t fsize = file.size();
      if (fsize > 0 && fsize < 152) {
        Serial.printf("DataStore: contacts3 truncated (%d bytes < 152), discarding\n", (int)fsize);
        file.close();
        fs->remove("/contacts3");
        if (fs->exists("/contacts3.tmp")) {
          File tmp = openRead(fs, "/contacts3.tmp");
          if (tmp && tmp.size() >= 152) {
            Serial.println("DataStore: recovering from .tmp after truncation");
            tmp.close();
            fs->rename("/contacts3.tmp", "/contacts3");
            file = openRead(fs, "/contacts3");
            if (!file) return;  // give up
          } else {
            if (tmp) tmp.close();
            Serial.println("DataStore: no valid contacts backup — starting fresh");
            return;
          }
        } else {
          Serial.println("DataStore: no .tmp backup — starting fresh");
          return;
        }
      } else if (fsize == 0) {
        // Empty file — nothing to load
        file.close();
        return;
      }

      bool full = false;
      while (!full) {
        ContactInfo c;
        uint8_t pub_key[32];
        uint8_t unused;

        bool success = (file.read(pub_key, 32) == 32);
        success = success && (file.read((uint8_t *)&c.name, 32) == 32);
        success = success && (file.read(&c.type, 1) == 1);
        success = success && (file.read(&c.flags, 1) == 1);
        success = success && (file.read(&unused, 1) == 1);
        success = success && (file.read((uint8_t *)&c.sync_since, 4) == 4); // was 'reserved'
        success = success && (file.read((uint8_t *)&c.out_path_len, 1) == 1);
        success = success && (file.read((uint8_t *)&c.last_advert_timestamp, 4) == 4);
        success = success && (file.read(c.out_path, 64) == 64);
        success = success && (file.read((uint8_t *)&c.lastmod, 4) == 4);
        success = success && (file.read((uint8_t *)&c.gps_lat, 4) == 4);
        success = success && (file.read((uint8_t *)&c.gps_lon, 4) == 4);

        if (!success) break; // EOF

        c.id = mesh::Identity(pub_key);
        if (!host->onContactLoaded(c)) full = true;
      }
      file.close();
    }
}

void DataStore::saveContacts(DataStoreHost* host) {
  FILESYSTEM* fs = _getContactsChannelsFS();

  // ESP32: atomic tmp+rename pattern (protects against SD card corruption on power loss)
  const char* finalPath = "/contacts3";
  const char* tmpPath   = "/contacts3.tmp";

  File file = openWrite(fs, tmpPath);
  if (!file) {
    Serial.println("DataStore: saveContacts FAILED — cannot open tmp file");
    return;
  }

  uint32_t idx = 0;
  ContactInfo c;
  uint8_t unused = 0;
  uint32_t recordsWritten = 0;
  bool writeOk = true;

  while (host->getContactForSave(idx, c)) {
    bool success = (file.write(c.id.pub_key, 32) == 32);
    success = success && (file.write((uint8_t *)&c.name, 32) == 32);
    success = success && (file.write(&c.type, 1) == 1);
    success = success && (file.write(&c.flags, 1) == 1);
    success = success && (file.write(&unused, 1) == 1);
    success = success && (file.write((uint8_t *)&c.sync_since, 4) == 4);
    success = success && (file.write((uint8_t *)&c.out_path_len, 1) == 1);
    success = success && (file.write((uint8_t *)&c.last_advert_timestamp, 4) == 4);
    success = success && (file.write(c.out_path, 64) == 64);
    success = success && (file.write((uint8_t *)&c.lastmod, 4) == 4);
    success = success && (file.write((uint8_t *)&c.gps_lat, 4) == 4);
    success = success && (file.write((uint8_t *)&c.gps_lon, 4) == 4);

    if (!success) {
      writeOk = false;
      Serial.printf("DataStore: saveContacts write error at record %d\n", idx);
      break;
    }

    recordsWritten++;
    idx++;
  }

  file.close();

  // Verify the write completed
  size_t expectedBytes = recordsWritten * 152;
  File verify = openRead(fs, tmpPath);
  size_t bytesWritten = verify ? verify.size() : 0;
  if (verify) verify.close();

  if (!writeOk || bytesWritten != expectedBytes) {
    Serial.printf("DataStore: saveContacts ABORTED — wrote %d bytes, expected %d (%d records)\n",
                  (int)bytesWritten, (int)expectedBytes, recordsWritten);
    fs->remove(tmpPath);
    return;
  }

  // Replace original with verified temp file
  fs->remove(finalPath);
  if (fs->rename(tmpPath, finalPath)) {
    Serial.printf("DataStore: saved %d contacts (%d bytes)\n", recordsWritten, (int)bytesWritten);
  } else {
    Serial.println("DataStore: rename failed, tmp file preserved");
  }
}

// =========================================================================
// Chunked contact save — non-blocking across multiple loop iterations
// Only for ESP32 with SD card — nRF52 uses blocking saveContacts() above
// =========================================================================

bool DataStore::beginSaveContacts(DataStoreHost* host) {
  if (_saveInProgress) return false;  // Already saving

  FILESYSTEM* fs = _getContactsChannelsFS();
  _saveFile = openWrite(fs, "/contacts3.tmp");
  if (!_saveFile) {
    Serial.println("DataStore: chunked save FAILED — cannot open tmp file");
    return false;
  }

  _saveHost = host;
  _saveIdx = 0;
  _saveRecordsWritten = 0;
  _saveWriteOk = true;
  _saveInProgress = true;
  Serial.println("DataStore: chunked save started");
  return true;
}

bool DataStore::saveContactsChunk(int batchSize) {
  if (!_saveInProgress || !_saveWriteOk) return false;

  ContactInfo c;
  uint8_t unused = 0;
  int written = 0;

  while (written < batchSize && _saveHost->getContactForSave(_saveIdx, c)) {
    bool success = (_saveFile.write(c.id.pub_key, 32) == 32);
    success = success && (_saveFile.write((uint8_t *)&c.name, 32) == 32);
    success = success && (_saveFile.write(&c.type, 1) == 1);
    success = success && (_saveFile.write(&c.flags, 1) == 1);
    success = success && (_saveFile.write(&unused, 1) == 1);
    success = success && (_saveFile.write((uint8_t *)&c.sync_since, 4) == 4);
    success = success && (_saveFile.write((uint8_t *)&c.out_path_len, 1) == 1);
    success = success && (_saveFile.write((uint8_t *)&c.last_advert_timestamp, 4) == 4);
    success = success && (_saveFile.write(c.out_path, 64) == 64);
    success = success && (_saveFile.write((uint8_t *)&c.lastmod, 4) == 4);
    success = success && (_saveFile.write((uint8_t *)&c.gps_lat, 4) == 4);
    success = success && (_saveFile.write((uint8_t *)&c.gps_lon, 4) == 4);

    if (!success) {
      _saveWriteOk = false;
      Serial.printf("DataStore: chunked save write error at record %d\n", _saveIdx);
      return false;  // Error — finishSaveContacts will clean up
    }

    _saveRecordsWritten++;
    _saveIdx++;
    written++;
  }

  // Check if there are more contacts to write
  ContactInfo peek;
  if (_saveHost->getContactForSave(_saveIdx, peek)) {
    return true;  // More to write
  }
  return false;  // Done
}

void DataStore::finishSaveContacts() {
  if (!_saveInProgress) return;

  _saveFile.close();
  _saveInProgress = false;

  FILESYSTEM* fs = _getContactsChannelsFS();
  const char* finalPath = "/contacts3";
  const char* tmpPath   = "/contacts3.tmp";

  // Verify
  size_t expectedBytes = _saveRecordsWritten * 152;
  File verify = openRead(fs, tmpPath);
  size_t bytesWritten = verify ? verify.size() : 0;
  if (verify) verify.close();

  if (!_saveWriteOk || bytesWritten != expectedBytes) {
    Serial.printf("DataStore: chunked save ABORTED — wrote %d bytes, expected %d (%d records)\n",
                  (int)bytesWritten, (int)expectedBytes, _saveRecordsWritten);
    fs->remove(tmpPath);
    return;
  }

  fs->remove(finalPath);
  if (fs->rename(tmpPath, finalPath)) {
    Serial.printf("DataStore: saved %d contacts (%d bytes, chunked)\n",
                  _saveRecordsWritten, (int)bytesWritten);
  } else {
    Serial.println("DataStore: rename failed, tmp file preserved");
  }
}

void DataStore::loadChannels(DataStoreHost* host) {
    FILESYSTEM* fs = _getContactsChannelsFS();

    // Crash recovery (same pattern as contacts)
    if (!fs->exists("/channels2") && fs->exists("/channels2.tmp")) {
      Serial.println("DataStore: recovering channels from .tmp file");
      fs->rename("/channels2.tmp", "/channels2");
    }
    if (fs->exists("/channels2.tmp")) {
      fs->remove("/channels2.tmp");
    }

    File file = openRead(fs, "/channels2");
    if (file) {
      bool full = false;
      uint8_t channel_idx = 0;
      while (!full) {
        ChannelDetails ch;
        uint8_t unused[4];

        bool success = (file.read(unused, 4) == 4);
        success = success && (file.read((uint8_t *)ch.name, 32) == 32);
        success = success && (file.read((uint8_t *)ch.channel.secret, 32) == 32);

        if (!success) break; // EOF

        if (host->onChannelLoaded(channel_idx, ch)) {
          channel_idx++;
        } else {
          full = true;
        }
      }
      file.close();
    }
}

void DataStore::saveChannels(DataStoreHost* host) {
  FILESYSTEM* fs = _getContactsChannelsFS();
  const char* finalPath = "/channels2";
  const char* tmpPath   = "/channels2.tmp";

  File file = openWrite(fs, tmpPath);
  if (!file) {
    Serial.println("DataStore: saveChannels FAILED — cannot open tmp file");
    return;
  }

  uint8_t channel_idx = 0;
  ChannelDetails ch;
  uint8_t unused[4];
  memset(unused, 0, 4);
  bool writeOk = true;

  while (host->getChannelForSave(channel_idx, ch)) {
    bool success = (file.write(unused, 4) == 4);
    success = success && (file.write((uint8_t *)ch.name, 32) == 32);
    success = success && (file.write((uint8_t *)ch.channel.secret, 32) == 32);

    if (!success) {
      writeOk = false;
      Serial.printf("DataStore: saveChannels write error at channel %d\n", channel_idx);
      break;
    }
    channel_idx++;
  }

  file.close();

  // Reopen read-only to get true on-disk size (SPIFFS file.size() is unreliable before close)
  size_t expectedBytes = channel_idx * 68;  // 4 + 32 + 32 = 68 bytes per channel
  File verify = openRead(fs, tmpPath);
  size_t bytesWritten = verify ? verify.size() : 0;
  if (verify) verify.close();
  if (!writeOk || bytesWritten != expectedBytes) {
    Serial.printf("DataStore: saveChannels ABORTED — wrote %d bytes, expected %d\n",
                  (int)bytesWritten, (int)expectedBytes);
    fs->remove(tmpPath);
    return;
  }

  fs->remove(finalPath);
  if (fs->rename(tmpPath, finalPath)) {
    Serial.printf("DataStore: saved %d channels (%d bytes)\n", channel_idx, (int)bytesWritten);
  } else {
    Serial.println("DataStore: channels rename failed, tmp file preserved");
  }
}

uint8_t DataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  char path[64];
  char fname[18];

  if (key_len > 8) key_len = 8; // just use first 8 bytes (prefix)
  mesh::Utils::toHex(fname, key, key_len);
  sprintf(path, "/bl/%s", fname);

  if (_fs->exists(path)) {
    File f = openRead(_fs, path);
    if (f) {
      int len = f.read(dest_buf, 255); // currently MAX 255 byte blob len supported!!
      f.close();
      return len;
    }
  }
  return 0; // not found
}

bool DataStore::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len) {
  char path[64];
  char fname[18];

  if (key_len > 8) key_len = 8; // just use first 8 bytes (prefix)
  mesh::Utils::toHex(fname, key, key_len);
  sprintf(path, "/bl/%s", fname);

  File f = openWrite(_fs, path);
  if (f) {
    int n = f.write(src_buf, len);
    f.close();
    if (n == len) return true; // success!

    _fs->remove(path); // blob was only partially written!
  }
  return false; // error
}