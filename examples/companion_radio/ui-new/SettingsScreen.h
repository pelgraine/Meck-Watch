#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#if defined(MECK_TWATCH)
#include <helpers/ui/LGFXDisplay.h>   // setTextWrap/getCursorX for the row ticker
#include "WatchEmoji.h"               // inline colour emoji in the Name row (watch)
// Row ticker scroll speed -- matches TW_TICKER_MS_PER_PX in TWatchComposeScreens.h.
#define SETTINGS_TICKER_MS_PER_PX 20
#endif
#include <helpers/ChannelDetails.h>
#include <helpers/TransportKeyStore.h>
#include <helpers/SensorManager.h>   // gps_position_locked, set by the Position row
#include "target.h"                  // the `sensors` global (node_lat / node_lon)
#include <MeshCore.h>
#include "../NodePrefs.h"
#include "MeckFonts.h"

// Inline edit hint shown next to values being adjusted
#if defined(MECK_TWATCH)
  #define EDIT_ADJ_HINT ""
#else
  #define EDIT_ADJ_HINT "<W/S>"
#endif


#ifdef MECK_WIFI_COMPANION
  #include <WiFi.h>
  #include <SD.h>
  #ifdef MECK_TWATCH
    #include <LittleFS.h>   // watch has no SD; creds persist on the LittleFS partition
  #endif
#endif

#if defined(MECK_TWATCH)
#include "WatchFileManager.h"
#endif
// Config export/import is available wherever there is somewhere to write it:
// SD on the T-Deck family, the internal LittleFS ("maps") on the watches.
#if defined(HAS_SDCARD) || defined(MECK_TWATCH)
  #define MECK_HAS_CONFIG_EXPORT 1
#endif
#ifdef MECK_OTA_UPDATE
  #ifndef MECK_WIFI_COMPANION
    #include <WiFi.h>
    #include <SD.h>
  #endif
  #include <WebServer.h>
  #include <DNSServer.h>
  #include <Update.h>
  #include <esp_ota_ops.h>
#endif

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;


// ---------------------------------------------------------------------------
// Auto-add config bitmask (mirrored from MyMesh.cpp for UI access)
// ---------------------------------------------------------------------------
#ifndef AUTO_ADD_OVERWRITE_OLDEST
#define AUTO_ADD_OVERWRITE_OLDEST (1 << 0)  // 0x01 - overwrite oldest non-favourite when full
#define AUTO_ADD_CHAT             (1 << 1)  // 0x02 - auto-add Chat (Companion) (ADV_TYPE_CHAT)
#define AUTO_ADD_REPEATER         (1 << 2)  // 0x04 - auto-add Repeater (ADV_TYPE_REPEATER)
#define AUTO_ADD_ROOM_SERVER      (1 << 3)  // 0x08 - auto-add Room Server (ADV_TYPE_ROOM)
#define AUTO_ADD_SENSOR           (1 << 4)  // 0x10 - auto-add Sensor (ADV_TYPE_SENSOR)
#endif

// All type bits combined (excludes overwrite flag)
#ifndef AUTO_ADD_ALL_TYPES
#define AUTO_ADD_ALL_TYPES (AUTO_ADD_CHAT | AUTO_ADD_REPEATER | \
                            AUTO_ADD_ROOM_SERVER | AUTO_ADD_SENSOR)
#endif

// Contact mode indices for picker
#define CONTACT_MODE_AUTO_ALL 0  // Add all contacts automatically
#define CONTACT_MODE_CUSTOM   1  // Per-type toggles
#define CONTACT_MODE_MANUAL   2  // No auto-add, companion app only
#define CONTACT_MODE_COUNT    3

// ---------------------------------------------------------------------------
// Export section flags (must match MeckExport.h)
// ---------------------------------------------------------------------------
#ifndef MECK_EXPORT_IDENTITY
#define MECK_EXPORT_IDENTITY  0x01
#define MECK_EXPORT_CHANNELS  0x02
#define MECK_EXPORT_CONTACTS  0x04
#define MECK_EXPORT_RADIO     0x08
#define MECK_EXPORT_AUTOADD   0x10
#define MECK_EXPORT_ALL       0x1F
#endif

// ---------------------------------------------------------------------------
// Radio presets (shared with Serial CLI in MyMesh.cpp)
// ---------------------------------------------------------------------------
#include "RadioPresets.h"

// ---------------------------------------------------------------------------
// GPS baud rate options (shared with UITask GPS home page overlay)
// ---------------------------------------------------------------------------
static const uint32_t GPS_BAUD_OPTIONS[] = { 0, 4800, 9600, 19200, 38400, 57600, 115200 };
#define GPS_BAUD_OPTION_COUNT 7

static inline const char* gpsBaudLabel(uint32_t baud, char* buf, int bufLen) {
  if (baud == 0) return "Default (38400)";
  snprintf(buf, bufLen, "%lu", (unsigned long)baud);
  return buf;
}

static inline int findGpsBaudIndex(uint32_t baud) {
  for (int i = 0; i < GPS_BAUD_OPTION_COUNT; i++) {
    if (GPS_BAUD_OPTIONS[i] == baud) return i;
  }
  return 0;
}

// Auto-lock timeout options (minutes, 0=disabled)
static const uint8_t AUTO_LOCK_OPTIONS[] = { 0, 2, 5, 10, 15, 30 };
#define AUTO_LOCK_OPTION_COUNT 6

static inline const char* autoLockLabel(uint8_t minutes) {
  if (minutes == 0) return "None";
  static char buf[8];
  snprintf(buf, sizeof(buf), "%d min", minutes);
  return buf;
}

static inline int findAutoLockIndex(uint8_t minutes) {
  for (int i = 0; i < AUTO_LOCK_OPTION_COUNT; i++) {
    if (AUTO_LOCK_OPTIONS[i] == minutes) return i;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Settings row types
// ---------------------------------------------------------------------------
enum SettingsRowType : uint8_t {
  ROW_NAME,           // Device name (text editor)
  ROW_RADIO_PRESET,   // Radio preset picker
  ROW_FREQ,           // Frequency (float)
  ROW_BW,             // Bandwidth (float)
  ROW_SF,             // Spreading factor (5-12)
  ROW_CR,             // Coding rate (5-8)
  ROW_TX_POWER,       // TX power (1-20 dBm)
  ROW_UTC_OFFSET,     // UTC offset (-12 to +14)
  ROW_BACKLIGHT_BRIGHTNESS,  // Backlight brightness % the heart button toggles to (MAX only)
  ROW_KB_BACKLIGHT,   // Keyboard LED brightness % (MAX only)
  ROW_MSG_NOTIFY,     // Keyboard flash on new msg toggle
  ROW_DARK_MODE,      // Dark mode toggle (inverted display)
  ROW_LARGE_FONT,     // Font size toggle: 0=tiny (default), 1=larger
  ROW_FONT_STYLE,     // Font style: Classic / Noto Sans / Montserrat
  ROW_GPS_BAUD,       // GPS baud rate picker (requires reboot)
  ROW_PATH_HASH_SIZE, // Path hash size (1, 2, or 3 bytes per hop)
  ROW_DEFAULT_SCOPE,  // Device-wide default region (text editor)
  ROW_POSITION_MODE,  // Position sharing: Off / Manual / Auto-update (GPS)
  ROW_LATITUDE,       // Manual latitude (text editor, locked in Auto)
  ROW_LONGITUDE,      // Manual longitude (text editor, locked in Auto)
  #ifdef MECK_WIFI_COMPANION
  ROW_WIFI_SETUP,     // WiFi SSID/password configuration
  ROW_WIFI_TOGGLE,    // WiFi radio on/off toggle
  #endif
  ROW_CONTACT_HEADER,  // "--- Contacts ---" separator
  ROW_CONTACT_MODE,    // Contact auto-add mode picker (Auto All / Custom / Manual)
  ROW_AUTOADD_CHAT,    // Toggle: auto-add Chat clients
  ROW_AUTOADD_REPEATER,// Toggle: auto-add Repeaters
  ROW_AUTOADD_ROOM,    // Toggle: auto-add Room Servers
  ROW_AUTOADD_SENSOR,  // Toggle: auto-add Sensors
  ROW_AUTOADD_OVERWRITE, // Toggle: overwrite oldest non-favourite when full
  ROW_CONTACTS_SUBMENU,  // Folder row → enters Contacts sub-screen
  ROW_CHANNELS_SUBMENU,  // Folder row → enters Channels sub-screen
  ROW_CH_HEADER,      // "--- Channels ---" separator
  ROW_CHANNEL,        // A channel entry (dynamic, index stored separately)
  ROW_ADD_CHANNEL,    // "+ Add Channel (# = public)"
  #ifdef MECK_HAS_CONFIG_EXPORT
  ROW_EXPORT_IMPORT_SUBMENU, // Folder row: "Export/Import >>"
  ROW_EXPORT_TO_SD,      // "Export to SD >>" (enters flags sub-screen)
  ROW_IMPORT_FROM_SD,    // "Import from SD" action
  ROW_EXPORT_IDENTITY,   // Checkbox: include identity in export
  ROW_EXPORT_RADIO,      // Checkbox: include radio settings
  ROW_EXPORT_CHANNELS,   // Checkbox: include channels
  ROW_EXPORT_CONTACTS,   // Checkbox: include contacts
  ROW_EXPORT_AUTOADD,    // Checkbox: include auto-add preferences (sub-item of contacts)
  ROW_EXPORT_NOW,        // ">> Export Now" action trigger
  #endif
  ROW_RXLOG,          // Rx Log packet sniffer (opens RxLogScreen)
  ROW_FILE_MANAGER,   // WiFi AP file explorer over LittleFS + SPIFFS (watch)
#if defined(MECK_TWATCH)
  ROW_EXPERIMENTAL_SUBMENU, // Folder row: enters Experimental Features sub-screen (watch)
  ROW_RX_DUTY_CYCLE,        // Toggle: MCU-driven RX duty cycle (watch)
  ROW_RX_DUTY_LEVEL,        // Cycle: nap depth Light/Medium/Aggressive (watch)
  ROW_RX_DUTY_UNAVAIL,      // Note shown when SF > 9 (feature not applicable)
  ROW_CLIENT_REPEAT,        // Companion client-repeat (flood forwarding) toggle
  ROW_CANNED_SUBMENU,       // Folder row: enters Canned Messages sub-screen (watch)
  ROW_CANNED_SLOT,          // A canned message slot; param = slot index 0..4 (watch)
#endif
  ROW_INFO_HEADER,    // "--- Info ---" separator
  #ifdef MECK_OTA_UPDATE
  ROW_OTA_TOOLS_SUBMENU, // Folder row → enters OTA Tools sub-screen
  ROW_FW_UPDATE,      // "Firmware Update" — WiFi upload + flash
  ROW_SD_FILE_MGR,    // "SD File Manager" — WiFi file browser
  #endif
  ROW_PUB_KEY,        // Public key display
  ROW_FIRMWARE,       // Firmware version
#if defined(MECK_TWATCH)
  ROW_MCU_TEMP,       // MCU (ESP32-S3 die) temperature, read-only (watch)
#endif
};

// ---------------------------------------------------------------------------
// Editing modes
// ---------------------------------------------------------------------------
enum EditMode : uint8_t {
  EDIT_NONE,         // Just browsing
  EDIT_TEXT,         // Typing into a text buffer (name, channel name)
  EDIT_PICKER,       // A/D cycles options (radio preset, contact mode)
  EDIT_NUMBER,       // W/S adjusts value (freq, BW, SF, CR, TX, UTC)
  EDIT_CONFIRM,      // Confirmation dialog (delete channel, apply radio)
  EDIT_NOTIF_SOUND,  // Sound picker for per-channel notification tone
  EDIT_SHARE_PICK,   // Contact picker for channel sharing
  #ifdef MECK_WIFI_COMPANION
  EDIT_WIFI,         // WiFi scan/select/password flow
  #endif
  #ifdef MECK_OTA_UPDATE
  EDIT_OTA,          // OTA firmware update flow (multi-phase overlay)
  EDIT_FILEMGR,      // SD file manager flow (WiFi file browser)
  #endif
};

// ---------------------------------------------------------------------------
// Settings sub-screens (collapsible sections)
// ---------------------------------------------------------------------------
enum SubScreen : uint8_t {
  SUB_NONE,        // Top-level settings list
  SUB_CONTACTS,    // Contacts settings sub-screen
  SUB_CHANNELS,    // Channels management sub-screen
#if defined(MECK_TWATCH)
  SUB_EXPERIMENTAL,  // Experimental Features sub-screen (watch)
  SUB_CANNED,        // Canned Messages sub-screen (watch)
#endif
  #ifdef MECK_OTA_UPDATE
  SUB_OTA_TOOLS,   // OTA Tools sub-screen (FW update + File Manager)
  #endif
  #ifdef MECK_HAS_CONFIG_EXPORT
  SUB_EXPORT_IMPORT,  // Export/Import menu
  SUB_EXPORT_FLAGS,   // Export checkboxes + trigger
  #endif
};

#ifdef MECK_OTA_UPDATE
// OTA update phases
enum OtaPhase : uint8_t {
  OTA_PHASE_CONFIRM,    // "Start firmware update? Enter:Yes Sh+Del:No"
  OTA_PHASE_AP_START,   // Starting WiFi AP + web server
  OTA_PHASE_WAITING,    // AP up, waiting for device to upload
  OTA_PHASE_RECEIVING,  // File upload in progress
  OTA_PHASE_VERIFY,     // Checking downloaded file
  OTA_PHASE_FLASH,      // Writing to flash — DO NOT POWER OFF
  OTA_PHASE_DONE,       // Success, rebooting
  OTA_PHASE_ERROR,      // Error with message
};

// File manager phases
enum FmPhase : uint8_t {
  FM_PHASE_CONFIRM,     // "Start SD file manager? Enter:Yes Sh+Del:No"
  FM_PHASE_WAITING,     // AP up, file browser active
  FM_PHASE_ERROR,       // Error with message
};
#endif

// Max rows in the settings list (increased for contact sub-toggles + WiFi)
#define SETTINGS_LORA_ANTENNA_ROWS 0
#if   defined(MECK_WIFI_COMPANION)
#define SETTINGS_MAX_ROWS (57 + SETTINGS_LORA_ANTENNA_ROWS)  // Extra rows for contacts + WiFi + scope + export
#else
#define SETTINGS_MAX_ROWS (55 + SETTINGS_LORA_ANTENNA_ROWS)  // Contacts section + scope + export
#endif
#define SETTINGS_TEXT_BUF  33  // 32 chars + null

class SettingsScreen : public UIScreen {
private:
  UITask* _task;
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;

  // Row table Ã¢â‚¬â€ rebuilt whenever channels change
  struct Row {
    SettingsRowType type;
    uint8_t param;       // channel index for ROW_CHANNEL, preset index for ROW_RADIO_PRESET
  };
  Row _rows[SETTINGS_MAX_ROWS];
  int _numRows;

  // Cursor & scroll
  int _cursor;        // selected row
  int _scrollTop;     // first visible row
  int _renderScreenH = 128;  // display.height() captured each render, for selectRowAtVY hit-testing

  // Editing state
  EditMode _editMode;
  char _editBuf[SETTINGS_TEXT_BUF];
  int _editPos;
  int _editPickerIdx;       // for preset picker / contact mode picker
  float _editFloat;         // for freq/BW editing
  int _editInt;             // for SF/CR/TX/UTC editing
  uint8_t _fontPickerOriginal;  // font style before edit (for cancel revert)
  int _confirmAction;       // 0=none, 1=delete channel, 2=apply radio

  // Notification sound picker state

  // Onboarding mode
  bool _onboarding;

  // Sub-screen navigation
  SubScreen _subScreen;
  int _savedTopCursor;  // cursor position to restore when leaving sub-screen
  #ifdef MECK_HAS_CONFIG_EXPORT
  int _savedExportCursor;  // cursor in SUB_EXPORT_IMPORT when entering SUB_EXPORT_FLAGS
  uint8_t _exportFlags;    // bitmask of MECK_EXPORT_* flags for export checkboxes
  bool _exportRequested;   // set by key handler, cleared by main.cpp after calling export
  bool _importRequested;   // set by key handler, cleared by main.cpp after calling import
  #endif

  bool _rxlogRequested = false;  // set by key handler, cleared by main.cpp after opening Rx Log

  // Channel share picker state
  #define SHARE_MAX_CONTACTS 32
  uint8_t _shareChannelIdx;            // channel being shared
  int _shareContacts[SHARE_MAX_CONTACTS]; // indices of DM-capable contacts
  int _shareContactCount;              // number of entries in _shareContacts
  int _sharePickerIdx;                 // highlighted item in picker
  int _sharePickerScroll;              // scroll offset in picker
  bool _shareRequested;                // flag for main.cpp
  int _shareContactIdx;                // selected contact index (for main.cpp)

  // Dirty flag for radio params Ã¢â‚¬â€ prompt to apply
  bool _radioChanged;

  // T5S3: signal UITask to open VKB when entering text edit mode
  bool _needsTextVKB;
  bool _wantsWatchChannels;   // Watch: hand off to WatchChannelConfigScreen (polled by UITask)
  bool _wantsKb;              // Watch: open the touch keyboard for the selected text row (polled by UITask)
  bool _wantsApplyRxDuty;     // Watch: apply the rx_duty_cycle toggle to radio_driver (polled by UITask)
  bool _wantsApplyRxLevel;    // Watch: apply the rx_duty_level (nap depth) to radio_driver (polled by UITask)
#ifdef MECK_WIFI_COMPANION
  bool _wantsWifiPwKb;        // Watch: open the touch keyboard for WiFi password entry (polled by UITask)
#endif
#if defined(MECK_TWATCH)
  char _kbInitText[CANNED_MSG_LEN] = "";  // initial text handed to the touch keyboard for the row being edited (sized for a full canned message)
#endif
#if defined(MECK_TWATCH)
  // Selected-row ticker (marquee for rows wider than the screen), mirroring
  // TWatchChannelScreen: 20 ms/px, 24 px gap between the looping copies.
  int           _tickerRow = -1;      // row index currently anchored
  unsigned long _tickerStartMs = 0;   // animation anchor
  int           _tickerTextW = 0;     // measured on pass 0 of the previous frame
  int           _nameEmojiW = 0;      // width of the Name row's inline emoji cells (added to the ticker measurement)
  bool          _tickerActive = false;
#endif

  // 4G modem state (runtime cache of config)

  #ifdef MECK_WIFI_COMPANION
  // WiFi setup sub-screen state
  enum WifiSetupPhase : uint8_t {
    WIFI_PHASE_IDLE,
    WIFI_PHASE_SCANNING,
    WIFI_PHASE_SELECT,      // W/S to pick SSID, Enter to select
    WIFI_PHASE_PASSWORD,    // Type password, Enter to connect
    WIFI_PHASE_CONNECTING,
  };
  WifiSetupPhase _wifiPhase;
  String _wifiSSIDs[10];
  int _wifiSSIDCount;
  int _wifiSSIDSelected;
  char _wifiPassBuf[64];
  int _wifiPassLen;
  unsigned long _wifiFormLastChar;  // For brief password reveal
  #endif

  #ifdef MECK_OTA_UPDATE
  // OTA update state
  OtaPhase _otaPhase;
  WebServer* _otaServer;
  File _otaFile;
  size_t _otaBytesReceived;
  bool _otaUploadOk;
  char _otaApName[24];
  const char* _otaError;
  // File manager state
  FmPhase _fmPhase;
  const char* _fmError;
  DNSServer* _dnsServer;
  #endif

  // ---------------------------------------------------------------------------
  // Contact mode helpers
  // ---------------------------------------------------------------------------

  // Determine current contact mode from prefs
  int getContactMode() const {
    if ((_prefs->manual_add_contacts & 1) == 0) {
      return CONTACT_MODE_AUTO_ALL;
    }
    // manual_add_contacts bit 0 is set — check if any type bits are enabled
    if ((_prefs->autoadd_config & AUTO_ADD_ALL_TYPES) != 0) {
      return CONTACT_MODE_CUSTOM;
    }
    return CONTACT_MODE_MANUAL;
  }

  // Get display label for a contact mode
  static const char* contactModeLabel(int mode) {
    switch (mode) {
      case CONTACT_MODE_AUTO_ALL: return "Auto All";
      case CONTACT_MODE_CUSTOM:   return "Custom";
      case CONTACT_MODE_MANUAL:   return "Manual Only";
      default:                    return "?";
    }
  }

  // Apply a contact mode selection from picker
  void applyContactMode(int mode) {
    switch (mode) {
      case CONTACT_MODE_AUTO_ALL:
        _prefs->manual_add_contacts &= ~1;  // clear bit 0 → auto all
        break;
      case CONTACT_MODE_CUSTOM:
        _prefs->manual_add_contacts |= 1;   // set bit 0 → selective
        // If no type bits are set, default to all types enabled
        if ((_prefs->autoadd_config & AUTO_ADD_ALL_TYPES) == 0) {
          _prefs->autoadd_config |= AUTO_ADD_ALL_TYPES;
        }
        break;
      case CONTACT_MODE_MANUAL:
        _prefs->manual_add_contacts |= 1;   // set bit 0 → selective
        _prefs->autoadd_config &= ~AUTO_ADD_ALL_TYPES;  // clear all type bits
        // Note: keeps AUTO_ADD_OVERWRITE_OLDEST bit unchanged
        break;
    }
    the_mesh.savePrefs();
    rebuildRows();  // show/hide sub-toggles
    Serial.printf("Settings: Contact mode = %s (manual=%d, autoadd=0x%02X)\n",
                  contactModeLabel(mode), _prefs->manual_add_contacts, _prefs->autoadd_config);
  }

  // ---------------------------------------------------------------------------
  // Row table management
  // ---------------------------------------------------------------------------

  void rebuildRows() {
    _numRows = 0;
#if defined(MECK_TWATCH)
    _tickerRow = -1;   // row indices are about to change; re-anchor the ticker
#endif

    if (_subScreen == SUB_CONTACTS) {
      // --- Contacts sub-screen: only contact-related rows ---
      addRow(ROW_CONTACT_MODE);
      if (getContactMode() == CONTACT_MODE_CUSTOM) {
        addRow(ROW_AUTOADD_CHAT);
        addRow(ROW_AUTOADD_REPEATER);
        addRow(ROW_AUTOADD_ROOM);
        addRow(ROW_AUTOADD_SENSOR);
        addRow(ROW_AUTOADD_OVERWRITE);
      }
    } else if (_subScreen == SUB_CHANNELS) {
      // --- Channels sub-screen: only channel-related rows ---
      // Scan ALL slots — companion app may write non-contiguously, and
      // gaps can appear after channel deletion if compaction is incomplete.
      for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
        ChannelDetails ch;
        if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
          addRow(ROW_CHANNEL, i);
        }
      }
      addRow(ROW_ADD_CHANNEL);
#if defined(MECK_TWATCH)
    } else if (_subScreen == SUB_EXPERIMENTAL) {
      // --- Experimental Features sub-screen (watch) ---
#if defined(MECK_RX_DUTY_CYCLE)
      // RX duty cycle only helps at SF9 and below (higher SF/longer symbols
      // gain nothing worth the wake cost); hide it entirely otherwise. Only
      // offered on builds whose radio has the SX126x hardware duty cycle.
      if (_prefs->sf <= 9) {
        addRow(ROW_RX_DUTY_CYCLE);
        addRow(ROW_RX_DUTY_LEVEL);
      } else {
        addRow(ROW_RX_DUTY_UNAVAIL);   // note: only available at SF9 and below
      }
#endif
      // Companion client-repeat (upstream MeshCore): forward flood packets
      // like a repeater. Only togglable on a whitelisted frequency.
      addRow(ROW_CLIENT_REPEAT);
    } else if (_subScreen == SUB_CANNED) {
      // --- Canned Messages sub-screen (watch): five editable slots ---
      for (uint8_t i = 0; i < CANNED_MSG_SLOTS; i++) {
        addRow(ROW_CANNED_SLOT, i);
      }
#endif
    #ifdef MECK_OTA_UPDATE
    } else if (_subScreen == SUB_OTA_TOOLS) {
      // --- OTA Tools sub-screen ---
      addRow(ROW_FW_UPDATE);
      addRow(ROW_SD_FILE_MGR);
    #endif
    #ifdef MECK_HAS_CONFIG_EXPORT
    } else if (_subScreen == SUB_EXPORT_IMPORT) {
      // --- Export/Import sub-screen ---
      addRow(ROW_EXPORT_TO_SD);
      addRow(ROW_IMPORT_FROM_SD);
    } else if (_subScreen == SUB_EXPORT_FLAGS) {
      // --- Export checkboxes + trigger ---
      addRow(ROW_EXPORT_IDENTITY);
      addRow(ROW_EXPORT_RADIO);
      addRow(ROW_EXPORT_CHANNELS);
      addRow(ROW_EXPORT_CONTACTS);
      addRow(ROW_EXPORT_AUTOADD);
      addRow(ROW_EXPORT_NOW);
    #endif
    } else {
      // --- Top-level settings list ---
      addRow(ROW_NAME);
      addRow(ROW_RADIO_PRESET);
      addRow(ROW_FREQ);
      addRow(ROW_BW);
      addRow(ROW_SF);
      addRow(ROW_CR);
      addRow(ROW_TX_POWER);
      addRow(ROW_UTC_OFFSET);
#if defined(MECK_TWATCH)
      // Canned Messages folder row -- kept high in the list so it lands on
      // the first settings page.
      addRow(ROW_CANNED_SUBMENU);
#endif
#if !defined(MECK_TWATCH)
      addRow(ROW_MSG_NOTIFY);
#endif
#if HAS_GPS
      addRow(ROW_GPS_BAUD);
#endif
      addRow(ROW_PATH_HASH_SIZE);
      addRow(ROW_DEFAULT_SCOPE);
      addRow(ROW_POSITION_MODE);
      // Lat/lon are only meaningful once a position is being shared. In Auto
      // they are shown but not editable (GPS owns them) -- see activate().
      if (_prefs->position_mode != 0) {
        addRow(ROW_LATITUDE);
        addRow(ROW_LONGITUDE);
      }
#if !defined(MECK_TWATCH)
      addRow(ROW_DARK_MODE);
#endif
#if !defined(MECK_TWATCH)
      addRow(ROW_LARGE_FONT);
      addRow(ROW_FONT_STYLE);
#endif
      #ifdef MECK_WIFI_COMPANION
      addRow(ROW_WIFI_SETUP);
      addRow(ROW_WIFI_TOGGLE);
      #endif

      // Folder rows for sub-screens
      addRow(ROW_CONTACTS_SUBMENU);
      addRow(ROW_CHANNELS_SUBMENU);
      #ifdef MECK_OTA_UPDATE
      addRow(ROW_OTA_TOOLS_SUBMENU);
      #endif
      #ifdef MECK_HAS_CONFIG_EXPORT
      addRow(ROW_EXPORT_IMPORT_SUBMENU);
      #endif

      // Rx Log packet sniffer (opens RxLogScreen)
      addRow(ROW_RXLOG);
#if defined(MECK_TWATCH)
      addRow(ROW_FILE_MANAGER);
#endif

#if defined(MECK_TWATCH)
      // Experimental Features (watch) -- placed above the Info section
      addRow(ROW_EXPERIMENTAL_SUBMENU);
#endif

      // Info section (stays at top level)
      addRow(ROW_INFO_HEADER);
      addRow(ROW_PUB_KEY);
      addRow(ROW_FIRMWARE);
#if defined(MECK_TWATCH)
      addRow(ROW_MCU_TEMP);   // read-only; value re-read on each redraw
#endif

    }

    // Clamp cursor
    if (_cursor >= _numRows) _cursor = _numRows - 1;
    if (_cursor < 0) _cursor = 0;
    skipNonSelectable(1);
  }

  void addRow(SettingsRowType type, uint8_t param = 0) {
    if (_numRows < SETTINGS_MAX_ROWS) {
      _rows[_numRows].type = type;
      _rows[_numRows].param = param;
      _numRows++;
    }
  }

  bool isSelectable(int idx) const {
    if (idx < 0 || idx >= _numRows) return false;
    SettingsRowType t = _rows[idx].type;
    return t != ROW_CH_HEADER && t != ROW_INFO_HEADER && t != ROW_CONTACT_HEADER
#if defined(MECK_TWATCH)
           && t != ROW_RX_DUTY_UNAVAIL
#endif
    ;  // ROW_CONTACTS_SUBMENU and ROW_CHANNELS_SUBMENU ARE selectable
  }

  void skipNonSelectable(int dir) {
    while (_cursor >= 0 && _cursor < _numRows && !isSelectable(_cursor)) {
      _cursor += dir;
    }
    if (_cursor < 0) _cursor = 0;
    if (_cursor >= _numRows) _cursor = _numRows - 1;
    ensureCursorVisible();
  }

  // Bring the cursor into the visible window WITHOUT re-centering, so an
  // independent pageScroll() view offset survives until the cursor moves.
  void ensureCursorVisible() {
    const int headerH = 14, footerH = 14, lineH = _prefs->smallLineH();
    int maxVisible = (_renderScreenH - headerH - footerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    if (_cursor < _scrollTop) _scrollTop = _cursor;
    else if (_cursor >= _scrollTop + maxVisible) _scrollTop = _cursor - maxVisible + 1;
    if (_scrollTop < 0) _scrollTop = 0;
    if (_numRows > maxVisible && _scrollTop > _numRows - maxVisible) _scrollTop = _numRows - maxVisible;
  }

  // ---------------------------------------------------------------------------
  // Radio preset detection
  // ---------------------------------------------------------------------------

  int detectCurrentPreset() const {
    for (int i = 0; i < (int)NUM_RADIO_PRESETS; i++) {
      const RadioPreset& p = RADIO_PRESETS[i];
      if (fabsf(_prefs->freq - p.freq) < 0.01f &&
          fabsf(_prefs->bw - p.bw) < 0.01f &&
          _prefs->sf == p.sf &&
          _prefs->cr == p.cr &&
          _prefs->tx_power_dbm == p.tx_power) {
        return i;
      }
    }
    return -1;  // Custom
  }

  // ---------------------------------------------------------------------------
  // Hashtag channel creation
  // ---------------------------------------------------------------------------

  // --- Position helpers -------------------------------------------------
  // The coordinates live in sensors.node_lat/node_lon (already persisted at
  // prefs offsets 40/48 by savePrefs), not in NodePrefs -- only the mode is
  // stored there.
  double positionLat() const { return sensors.node_lat; }
  double positionLon() const { return sensors.node_lon; }
  void setPosition(double lat, double lon) {
    sensors.node_lat = lat;
    sensors.node_lon = lon;
  }

  // Keep the two derived pieces of state in step with position_mode:
  //   - the GPS write-lock, so Manual coordinates are not overwritten
  //   - advert_loc_policy, which is what createSelfAdvert actually checks
  // Mirrors the Meck-P4 derivation (advert policy = position_mode > 0).
  void applyPositionMode() {
    SensorManager::gps_position_locked = (_prefs->position_mode == 1);
    _prefs->advert_loc_policy =
        (_prefs->position_mode > 0) ? ADVERT_LOC_SHARE : ADVERT_LOC_NONE;
  }

  void createChannel(const char* name) {
    ChannelDetails newCh;
    memset(&newCh, 0, sizeof(newCh));

    if (name[0] == '#') {
      // Public hashtag channel -- derive secret from SHA-256 of name
      strncpy(newCh.name, name, sizeof(newCh.name));
      newCh.name[31] = '\0';

      uint8_t hash[32];
      mesh::Utils::sha256(hash, 32, (const uint8_t*)name, strlen(name));
      memcpy(newCh.channel.secret, hash, 16);
      Serial.printf("Settings: Creating public channel '%s'\n", name);
    } else {
      // Private channel -- random 16-byte secret
      strncpy(newCh.name, name, sizeof(newCh.name));
      newCh.name[31] = '\0';

      uint8_t secret[16];
      uint32_t r;
      for (int i = 0; i < 16; i++) {
        if (i % 4 == 0) r = esp_random();
        secret[i] = (r >> ((i % 4) * 8)) & 0xFF;
      }
      memcpy(newCh.channel.secret, secret, 16);
      Serial.printf("Settings: Creating private channel '%s'\n", name);
    }

    // Find next empty slot
    for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails existing;
      if (!the_mesh.getChannel(i, existing) || existing.name[0] == '\0') {
        if (the_mesh.setChannel(i, newCh)) {
          the_mesh.saveChannels();
          Serial.printf("Settings: Channel '%s' created at idx %d\n", newCh.name, i);
        }
        break;
      }
    }
  }


  void deleteChannel(uint8_t idx) {
    // Clear the channel by writing an empty ChannelDetails
    // Then compact: shift all channels above it down by one
    ChannelDetails empty;
    memset(&empty, 0, sizeof(empty));

    // Find highest used channel slot (scan all — gaps may exist)
    int total = 0;
    for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
        total = i + 1;
      }
    }

    // Shift channels down
    for (int i = idx; i < total - 1; i++) {
      ChannelDetails next;
      if (the_mesh.getChannel(i + 1, next)) {
        the_mesh.setChannel(i, next);
      }
    }
    // Clear the last slot
    the_mesh.setChannel(total - 1, empty);
    the_mesh.saveChannels();
    Serial.printf("Settings: Deleted channel at idx %d, compacted %d channels\n", idx, total);
  }

  // ---------------------------------------------------------------------------
  // Apply radio parameters live
  // ---------------------------------------------------------------------------

  void applyRadioParams() {
    radio_set_params(_prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
    radio_set_tx_power(_prefs->tx_power_dbm);
    the_mesh.savePrefs();
    the_mesh.resetRxPacketCount();   // zero the radio-page RX counter on radio param change
    _radioChanged = false;
    Serial.printf("Settings: Radio params applied - %.3f/%g/%d/%d TX:%d\n",
                  _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr, _prefs->tx_power_dbm);
  }

public:
  SettingsScreen(UITask* task, mesh::RTCClock* rtc, NodePrefs* prefs)
    : _task(task), _rtc(rtc), _prefs(prefs),
      _numRows(0), _cursor(0), _scrollTop(0),
      _editMode(EDIT_NONE), _editPos(0), _editPickerIdx(0),
      _editFloat(0), _editInt(0), _fontPickerOriginal(0), _confirmAction(0),
      _onboarding(false), _subScreen(SUB_NONE), _savedTopCursor(0),
      _radioChanged(false), _needsTextVKB(false), _wantsWatchChannels(false), _wantsKb(false), _wantsApplyRxDuty(false), _wantsApplyRxLevel(false) {
    memset(_editBuf, 0, sizeof(_editBuf));
#ifdef MECK_WIFI_COMPANION
    _wantsWifiPwKb = false;
#endif
    #ifdef MECK_HAS_CONFIG_EXPORT
    _savedExportCursor = 0;
    _exportFlags = 0x1F;  // MECK_EXPORT_ALL
    _exportRequested = false;
    _importRequested = false;
    #endif
    _shareChannelIdx = 0;
    _shareContactCount = 0;
    _sharePickerIdx = 0;
    _sharePickerScroll = 0;
    _shareRequested = false;
    _shareContactIdx = -1;
    #ifdef MECK_OTA_UPDATE
    _otaServer = nullptr;
    _otaPhase = OTA_PHASE_CONFIRM;
    _otaBytesReceived = 0;
    _otaUploadOk = false;
    _otaError = nullptr;
    _fmPhase = FM_PHASE_CONFIRM;
    _fmError = nullptr;
    _dnsServer = nullptr;
    #endif
  }

  void enter() {
    _editMode = EDIT_NONE;
    _subScreen = SUB_NONE;
    _savedTopCursor = 0;
    _cursor = 0;
    _scrollTop = 0;
    _radioChanged = false;
    #ifdef MECK_HAS_CONFIG_EXPORT
    _savedExportCursor = 0;
    _exportFlags = 0x1F;  // MECK_EXPORT_ALL
    _exportRequested = false;
    _importRequested = false;
    #endif
    _shareRequested = false;
    _shareContactIdx = -1;
    _shareContactCount = 0;
    #ifdef MECK_WIFI_COMPANION
    _wifiPhase = WIFI_PHASE_IDLE;
    _wifiSSIDCount = 0;
    _wifiSSIDSelected = 0;
    _wifiPassLen = 0;
    memset(_wifiPassBuf, 0, sizeof(_wifiPassBuf));
    _wifiFormLastChar = 0;
    #endif
    rebuildRows();
  }

  void enterOnboarding() {
    enter();
    _onboarding = true;
    // Start editing the device name immediately
    _cursor = 0;  // ROW_NAME
    startEditText(_prefs->node_name);
  }

  bool isOnboarding() const { return _onboarding; }
  bool isEditing() const { return _editMode != EDIT_NONE; }
  bool hasRadioChanges() const { return _radioChanged; }
  bool isOnChannelsSubScreen() const { return _subScreen == SUB_CHANNELS; }
  bool isOnDeletableChannel() const {
    return _subScreen == SUB_CHANNELS &&
           _cursor >= 0 && _cursor < _numRows &&
           _rows[_cursor].type == ROW_CHANNEL &&
           _rows[_cursor].param > 0;
  }

  // Tap-to-select: given a virtual Y coordinate, compute which row was tapped
  // and move cursor there. Returns: 0=miss, 1=moved to new row, 2=tapped current row.
  int selectRowAtVY(int vy) {
    if (_editMode != EDIT_NONE) return 0;  // Don't change cursor while editing
    const int headerH = 14, footerH = 14, lineH = _prefs->smallLineH();
    // bodyTop must match where the visual rows start (highlight bar position).
    // T5S3 renders highlight at y directly. T-Deck Pro offsets by smallHighlightOff().
    const int bodyTop = headerH + _prefs->smallHighlightOff();
    // vy arrives in the 128-tall virtual touch space; rows were drawn in the
    // display's logical space (_renderScreenH). Convert back so the hit-test
    // matches what was rendered, and use the render's own _scrollTop rather
    // than recomputing a scroll window (the two used a different maxVisible and
    // drifted apart toward the bottom of the list).
    int ly = (int)((long)vy * _renderScreenH / 128);
    const int maxY = _renderScreenH - footerH;
    if (ly < bodyTop || ly >= maxY) return 0;  // Outside body area

    int tappedRow = _scrollTop + (ly - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= _numRows) return 0;

    // Skip non-selectable rows (headers/separators)
    if (!isSelectable(tappedRow)) return 0;

    if (tappedRow == _cursor) return 2;  // Same row — activate
    _cursor = tappedRow;
    ensureCursorVisible();
    return 1;  // Moved to new row
  }

#if defined(MECK_TWATCH)
  // Long-press aim (main.cpp mapTouchLongPress). Identical to selectRowAtVY()
  // everywhere except the Canned Messages sub-screen, where a hold whose
  // contact point sits in the top edgePx of the row directly below the
  // highlight keeps the highlight instead of re-aiming. Fingertip centroids
  // register a few px below the perceived aim point (2-7 px in the TouchDiag
  // captures, Aug 2026, sensor range and render/hit geometry both verified
  // clean); on these 9 px rows a centre-aim at the pre-selected first slot
  // lands just inside row 2's band and silently opened it. Taps are
  // unaffected -- a mis-selected highlight is visible and self-correcting,
  // a silent activation retarget is not.
  int selectRowForLongPress(int vy) {
    if (_subScreen != SUB_CANNED) return selectRowAtVY(vy);
    // Geometry below must match selectRowAtVY() (and the render loop).
    const int headerH = 14, lineH = _prefs->smallLineH();
    const int bodyTop = headerH + _prefs->smallHighlightOff();
    int ly = (int)((long)vy * _renderScreenH / 128);
    int tappedRow = _scrollTop + (ly - bodyTop) / lineH;
    const int edgePx = 2;
    if (tappedRow == _cursor + 1 && (ly - bodyTop) % lineH < edgePx) {
      return 2;  // Knife-edge press on the next band: activate the highlight
    }
    return selectRowAtVY(vy);
  }
#endif

  // Page the list VIEW by one screenful (watch vertical swipe). Scrolls the
  // viewport only; the cursor/selection stays put (the highlight reappears
  // when its row scrolls back into view, and only moves when the user taps).
  void pageScroll(bool up) {
    if (_editMode != EDIT_NONE) return;
    const int headerH = 14, footerH = 14, lineH = _prefs->smallLineH();
    int maxVisible = (_renderScreenH - headerH - footerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    if (_numRows <= maxVisible) return;
    _scrollTop += up ? -maxVisible : maxVisible;
    if (_scrollTop < 0) _scrollTop = 0;
    if (_scrollTop > _numRows - maxVisible) _scrollTop = _numRows - maxVisible;
  }

  // ---------------------------------------------------------------------------
  // WiFi scan helpers
  // ---------------------------------------------------------------------------

  #ifdef MECK_WIFI_COMPANION
  // Perform a blocking WiFi scan. Populates _wifiSSIDs/_wifiSSIDCount and
  // advances _wifiPhase to SELECT (even on zero results, so the overlay
  // stays visible and the user can rescan with 'r').
  void performWifiScan() {
    _wifiPhase = WIFI_PHASE_SCANNING;
    _wifiSSIDCount = 0;
    _wifiSSIDSelected = 0;

    // Disconnect any active WiFi connection first — the ESP32 driver
    // returns -2 (WIFI_SCAN_FAILED) if the radio is busy with an
    // existing connection or the TCP companion server socket.
    WiFi.disconnect(false);   // false = don't turn off WiFi radio
    delay(100);               // let the driver settle
    WiFi.mode(WIFI_STA);

    // 500ms per-channel dwell helps detect phone hotspots that are slow
    // to respond to probe requests (default 300ms often misses them).
    int n = WiFi.scanNetworks(false, false, false, 500);
    Serial.printf("Settings: WiFi scan found %d networks\n", n);

    if (n > 0) {
      _wifiSSIDCount = min(n, 10);
      for (int si = 0; si < _wifiSSIDCount; si++) {
        _wifiSSIDs[si] = WiFi.SSID(si);
        Serial.printf("  [%d] %s (RSSI %d)\n", si,
                      _wifiSSIDs[si].c_str(), WiFi.RSSI(si));
      }
    } else if (n < 0) {
      Serial.printf("Settings: WiFi scan error %d\n", n);
    }
    WiFi.scanDelete();
    _wifiPhase = WIFI_PHASE_SELECT;  // always show overlay (even if 0)
  }

  // Persist the chosen SSID/password and attempt the connection. Called by the
  // physical-keyboard '\r' path and (on the watch) by submitWifiPassword after
  // touch-keyboard entry. Assumes _wifiSSIDs[_wifiSSIDSelected] and _wifiPassBuf
  // are populated.
  void commitWifiConnect() {
    _wifiPhase = WIFI_PHASE_CONNECTING;

    // Save credentials so the connection can be restored later.
#ifdef MECK_TWATCH
    // Watch: no SD card -- persist on the LittleFS partition.
    {
      File f = LittleFS.open("/wifi.cfg", "w");
      if (f) {
        f.println(_wifiSSIDs[_wifiSSIDSelected]);
        f.println(_wifiPassBuf);
        f.close();
      }
    }
#else
    // Save credentials to SD first (so web reader can reuse them)
    if (SD.exists("/web") || SD.mkdir("/web")) {
      File f = SD.open("/web/wifi.cfg", FILE_WRITE);
      if (f) {
        f.println(_wifiSSIDs[_wifiSSIDSelected]);
        f.println(_wifiPassBuf);
        f.close();
      }
      digitalWrite(SDCARD_CS, HIGH);
    }
#endif

    WiFi.disconnect(false);
    WiFi.begin(_wifiSSIDs[_wifiSSIDSelected].c_str(), _wifiPassBuf);

    // Brief blocking wait -- fine for e-ink (screen won't update during this anyway)
    unsigned long timeout = millis() + 8000;
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("Settings: WiFi connected to %s, IP: %s\n",
                    _wifiSSIDs[_wifiSSIDSelected].c_str(),
                    WiFi.localIP().toString().c_str());
      _editMode = EDIT_NONE;
      _wifiPhase = WIFI_PHASE_IDLE;
      if (_onboarding) _onboarding = false;  // Finish onboarding
    } else {
      Serial.println("Settings: WiFi connection failed");
      // Go back to SSID selection so user can retry
      _wifiPhase = WIFI_PHASE_SELECT;
    }
  }

  // After WiFi setup exits (connect success or user quit), try to
  // reconnect to saved credentials so the companion TCP server works.
  void wifiReconnectSaved() {
#ifdef MECK_TWATCH
    // Watch: prefer an on-device saved network (LittleFS). If none is stored,
    // fall back to the compile-time WIFI_SSID/WIFI_PWD default.
    {
      File f = LittleFS.open("/wifi.cfg", "r");
      if (f) {
        String ssid = f.readStringUntil('\n'); ssid.trim();
        String pass = f.readStringUntil('\n'); pass.trim();
        f.close();
        if (ssid.length() > 0) {
          Serial.printf("Settings: Reconnecting to saved WiFi '%s'\n", ssid.c_str());
          WiFi.begin(ssid.c_str(), pass.c_str());
          return;
        }
      }
    }
  #ifdef WIFI_SSID
    Serial.printf("Settings: Reconnecting to default WiFi '%s'\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PWD);
  #endif
#elif defined(WIFI_SSID)
    // Compile-time credentials: reconnect to the build-flag network (no SD).
    Serial.printf("Settings: Reconnecting to WiFi '%s'\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PWD);
#else
    File f = SD.open("/web/wifi.cfg", FILE_READ);
    if (f) {
      String ssid = f.readStringUntil('\n'); ssid.trim();
      String pass = f.readStringUntil('\n'); pass.trim();
      f.close();
      digitalWrite(SDCARD_CS, HIGH);
      if (ssid.length() > 0) {
        Serial.printf("Settings: Reconnecting to saved WiFi '%s'\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());
      }
    } else {
      digitalWrite(SDCARD_CS, HIGH);
    }
#endif
  }

  // Pro physical-keyboard back-out: UITask uses these to send the WiFi
  // password phase back to SSID selection on Shift+Backspace (so 'q'/'Q'
  // remain literal password characters).
  bool isInWifiPasswordEntry() const {
    return _editMode == EDIT_WIFI && _wifiPhase == WIFI_PHASE_PASSWORD;
  }
  void wifiPasswordBack() { _wifiPhase = WIFI_PHASE_SELECT; }

  // True while the WiFi network picker is showing; UITask uses this so
  // Shift+Backspace can exit the picker.
  bool isInWifiNetworkSelect() const {
    return _editMode == EDIT_WIFI && _wifiPhase == WIFI_PHASE_SELECT;
  }

#ifdef MECK_WIFI_COMPANION
  // Watch: bridge to open the touch keyboard for WiFi password entry, mirroring
  // the _wantsKb pattern for text rows. Polled and cleared by UITask, which
  // opens TWKB_WIFI_PASSWORD.
  bool wantsWifiPwKeyboard() const { return _wantsWifiPwKb; }
  void clearWantsWifiPwKeyboard() { _wantsWifiPwKb = false; }

  // Watch: map a tap (128-tall virtual Y) to an SSID row in the select overlay.
  // Geometry mirrors the render in renderWifiOverlay: box at by=14, first row at
  // by + 4 + 10 (after the "Select network:" header), each row 8px tall in the
  // display's logical space. Returns: 0 = miss, 1 = moved selection,
  // 2 = tapped the already-selected row (caller treats as Enter/confirm).
  int selectWifiSSIDAtVY(int vy) {
    if (_editMode != EDIT_WIFI || _wifiPhase != WIFI_PHASE_SELECT) return 0;
    if (_wifiSSIDCount == 0) return 0;
    const int by = 14;
    const int firstRowY = by + 4 + 10;   // header "Select network:" occupies by+4, then wy += 10
    const int rowH = 8;
    int ly = (int)((long)vy * _renderScreenH / 128);
    if (ly < firstRowY) return 0;
    int row = (ly - firstRowY) / rowH;
    if (row < 0 || row >= _wifiSSIDCount) return 0;
    if (row == _wifiSSIDSelected) return 2;  // tapped current -> confirm
    _wifiSSIDSelected = row;
    return 1;
  }

  // Watch: receive the typed password from the touch keyboard, then connect.
  // Mirrors what the physical-keyboard '\r' path does inline.
  void submitWifiPassword(const char* pass) {
    _wifiPassLen = 0;
    memset(_wifiPassBuf, 0, sizeof(_wifiPassBuf));
    if (pass) {
      for (int i = 0; pass[i] && _wifiPassLen < 63; i++) {
        _wifiPassBuf[_wifiPassLen++] = pass[i];
      }
      _wifiPassBuf[_wifiPassLen] = '\0';
    }
    commitWifiConnect();
  }
#endif


  #endif

  // T5S3 VKB integration for text editing (channel name, device name, freq, APN)
  bool needsTextVKB() const { return _needsTextVKB; }
  void clearTextNeedsVKB() { _needsTextVKB = false; }
  bool wantsWatchChannels() const { return _wantsWatchChannels; }
  void clearWantsWatchChannels() { _wantsWatchChannels = false; }
#if defined(MECK_TWATCH)
  // Watch touch-keyboard bridge for text rows (Name, Default Region,
  // Frequency, Add Channel).
  bool wantsKeyboard() const { return _wantsKb; }
  void clearWantKeyboard() { _wantsKb = false; }
  // Watch: Rx Duty Cycle toggle apply bridge (polled by UITask, which reaches
  // radio_driver). getRxDutyCycleEnabled() reflects the persisted pref.
  bool wantsApplyRxDuty() const { return _wantsApplyRxDuty; }
  void clearApplyRxDuty() { _wantsApplyRxDuty = false; }
  bool getRxDutyCycleEnabled() const { return _prefs->rx_duty_cycle != 0; }
  // Nap-depth apply bridge (polled by UITask alongside the on/off apply).
  bool wantsApplyRxLevel() const { return _wantsApplyRxLevel; }
  void clearApplyRxLevel() { _wantsApplyRxLevel = false; }
  uint8_t getRxDutyLevel() const { return _prefs->rx_duty_level; }
  const char* getKbInitialText() const { return _kbInitText; }
  bool isOnDefaultRegionRow() const {
    return _cursor >= 0 && _cursor < _numRows && _rows[_cursor].type == ROW_DEFAULT_SCOPE;
  }
  bool isOnCannedSlotRow() const {
    return _cursor >= 0 && _cursor < _numRows && _rows[_cursor].type == ROW_CANNED_SLOT;
  }
  // Apply keyboard-composed text to the selected row through the normal
  // startEditText + submitEditText commit path, so all side effects (bounds,
  // savePrefs, scope key derivation, empty-region-clears, freq constrain,
  // createChannel) match inline edits. The startEditText initial is
  // irrelevant here -- submitEditText replaces the buffer before commit.
  bool applyComposedField(const char* text) {
    SettingsRowType t = _rows[_cursor].type;
    if (t == ROW_CANNED_SLOT) {
      // Canned slots bypass the _editBuf commit path: SETTINGS_TEXT_BUF is
      // 33 while a slot holds up to 133 chars. Empty text clears the slot.
      uint8_t slot = _rows[_cursor].param;
      if (slot >= CANNED_MSG_SLOTS) return false;
      strncpy(_prefs->canned_msgs[slot], text ? text : "", CANNED_MSG_LEN - 1);
      _prefs->canned_msgs[slot][CANNED_MSG_LEN - 1] = '\0';
      the_mesh.savePrefs();
      return true;
    }
    if (t != ROW_NAME && t != ROW_DEFAULT_SCOPE &&
        t != ROW_FREQ && t != ROW_ADD_CHANNEL && t != ROW_TX_POWER &&
        t != ROW_BW && t != ROW_LATITUDE && t != ROW_LONGITUDE) {
      return false;  // row changed underneath
    }
    startEditText("");
    submitEditText(text ? text : "");
    return true;
  }

  // Typed-entry clamp/snap toast: queued by the ROW_FREQ / ROW_TX_POWER /
  // ROW_BW commit paths below when a typed value had to be adjusted, and
  // consumed by UITask straight after applyComposedField() so the toast
  // shows over the settings screen. Mirrors the canned-messages alert flag.
  bool consumeClampAlert(char* out, size_t out_sz) {
    if (_pendingClampAlert[0] == 0) return false;
    strncpy(out, _pendingClampAlert, out_sz - 1);
    out[out_sz - 1] = '\0';
    _pendingClampAlert[0] = 0;
    return true;
  }
private:
  char _pendingClampAlert[28] = "";
  void queueClampAlert(const char* msg) {
    strncpy(_pendingClampAlert, msg, sizeof(_pendingClampAlert) - 1);
    _pendingClampAlert[sizeof(_pendingClampAlert) - 1] = '\0';
  }
public:
#endif
  const char* getEditBuf() const { return _editBuf; }
  SettingsRowType getCurrentRowType() const { return _rows[_cursor].type; }
  void submitEditText(const char* text) {
    strncpy(_editBuf, text, SETTINGS_TEXT_BUF - 1);
    _editBuf[SETTINGS_TEXT_BUF - 1] = '\0';
    _editPos = strlen(_editBuf);
    // Simulate Enter to confirm the edit through the normal path
    handleInput('\r');
  }

  // Export/Import request flags — checked and cleared by main.cpp
  #ifdef MECK_HAS_CONFIG_EXPORT
  bool isExportRequested() const { return _exportRequested; }
  uint8_t getExportFlags() const { return _exportFlags; }
  void clearExportRequest() { _exportRequested = false; }
  bool isImportRequested() const { return _importRequested; }
  void clearImportRequest() { _importRequested = false; }
  #endif

  // Rx Log open request -- checked and cleared by main.cpp
  bool isRxLogRequested() const { return _rxlogRequested; }
  void clearRxLogRequest() { _rxlogRequested = false; }

  // Channel share request -- checked and cleared by main.cpp
  bool isShareRequested() const { return _shareRequested; }
  int getShareContactIdx() const { return _shareContactIdx; }
  uint8_t getShareChannelIdx() const { return _shareChannelIdx; }
  void clearShareRequest() { _shareRequested = false; _shareContactIdx = -1; }

  // ---------------------------------------------------------------------------
  // OTA firmware update
  // ---------------------------------------------------------------------------

  #ifdef MECK_OTA_UPDATE

  // HTML upload page served to the browser
  static const char* otaUploadPageHTML() {
    return
      "<!DOCTYPE html><html><head>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Meck Firmware Update</title>"
      "<style>"
      "body{font-family:-apple-system,sans-serif;max-width:480px;margin:40px auto;"
      "padding:0 20px;background:#1a1a2e;color:#e0e0e0}"
      "h1{color:#4ecca3;font-size:1.4em}"
      ".info{background:#16213e;padding:12px;border-radius:8px;margin:16px 0;font-size:0.9em}"
      "input[type=file]{margin:16px 0;color:#e0e0e0}"
      "button{background:#4ecca3;color:#1a1a2e;border:none;padding:12px 32px;"
      "border-radius:6px;font-size:1.1em;font-weight:bold;cursor:pointer}"
      "button:active{background:#3ba88f}"
      "#prog{display:none;margin-top:16px}"
      ".bar{background:#16213e;border-radius:4px;height:24px;overflow:hidden}"
      ".fill{background:#4ecca3;height:100%;width:0%;transition:width 0.3s}"
      "</style></head><body>"
      "<h1>Meck Firmware Update</h1>"
      "<div class='info'>Select the firmware .bin file and tap Upload. "
      "The device will verify and flash it automatically.</div>"
      "<form method='POST' action='/upload' enctype='multipart/form-data'>"
      "<input type='file' name='firmware' accept='.bin'><br>"
      "<button type='submit' onclick=\"document.getElementById('prog').style.display='block'\">"
      "Upload Firmware</button></form>"
      "<div id='prog'><div>Uploading... do not close this page</div>"
      "<div class='bar'><div class='fill' id='fill'></div></div></div>"
      "<script>document.querySelector('form').onsubmit=function(){"
      "var f=document.getElementById('fill'),w=0;"
      "setInterval(function(){w+=2;if(w>90)w=90;f.style.width=w+'%'},500)};</script>"
      "</body></html>";
  }

  void startOTA() {
    _editMode = EDIT_OTA;
    _otaPhase = OTA_PHASE_CONFIRM;
    _otaBytesReceived = 0;
    _otaUploadOk = false;
    _otaError = nullptr;
  }

  void startOTAServer() {
    // Build AP name with last 4 of MAC for uniqueness
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(_otaApName, sizeof(_otaApName), "Meck-Update-%02X%02X", mac[4], mac[5]);

    // Pause LoRa radio — SD and LoRa share the same SPI bus on both
    // platforms. Incoming packets during SD writes cause bus contention
    // that stalls the upload.
    extern void otaPauseRadio();
    otaPauseRadio();

    // Clean WiFi init from any state (including never-initialised on
    // standalone builds where WiFi.mode() was never called during boot).
    // OFF→AP sequence ensures the WiFi peripheral starts fresh.
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_otaApName);
    delay(500);  // Let AP stabilise
    Serial.printf("OTA: AP '%s' started, IP: %s\n",
                  _otaApName, WiFi.softAPIP().toString().c_str());

    // Start web server
    if (_otaServer) { _otaServer->stop(); delete _otaServer; }
    _otaServer = new WebServer(80);

    _otaServer->on("/", HTTP_GET, [this]() {
      _otaServer->send(200, "text/html", otaUploadPageHTML());
    });

    _otaServer->on("/upload", HTTP_POST,
      // Response after upload completes
      [this]() {
        _otaServer->send(200, "text/html",
          _otaUploadOk
            ? "<html><body style='background:#1a1a2e;color:#4ecca3;font-family:sans-serif;"
              "text-align:center;padding:60px'><h1>Upload OK!</h1>"
              "<p>The device is now verifying and flashing.<br>It will reboot automatically.</p></body></html>"
            : "<html><body style='background:#1a1a2e;color:#e74c3c;font-family:sans-serif;"
              "text-align:center;padding:60px'><h1>Upload Failed</h1>"
              "<p>Please try again.</p></body></html>"
        );
      },
      // Upload handler — called per chunk
      [this]() {
        HTTPUpload& upload = _otaServer->upload();

        if (upload.status == UPLOAD_FILE_START) {
          Serial.printf("OTA: Receiving: %s\n", upload.filename.c_str());
          _otaUploadOk = false;
          _otaBytesReceived = 0;

          if (!SD.exists("/firmware")) SD.mkdir("/firmware");
          if (SD.exists("/firmware/update.bin")) {
            SD.remove("/firmware/previous.bin");
            SD.rename("/firmware/update.bin", "/firmware/previous.bin");
          }
          _otaFile = SD.open("/firmware/update.bin", FILE_WRITE);
          if (!_otaFile) {
            Serial.println("OTA: Failed to open SD file");
            return;
          }
          _otaPhase = OTA_PHASE_RECEIVING;

        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (_otaFile) {
            _otaFile.write(upload.buf, upload.currentSize);
            _otaBytesReceived += upload.currentSize;
          }

        } else if (upload.status == UPLOAD_FILE_END) {
          if (_otaFile) {
            _otaFile.close();
            digitalWrite(SDCARD_CS, HIGH);
            Serial.printf("OTA: Received %d bytes\n", _otaBytesReceived);
            _otaUploadOk = (_otaBytesReceived > 0);
          }

        } else if (upload.status == UPLOAD_FILE_ABORTED) {
          if (_otaFile) { _otaFile.close(); SD.remove("/firmware/update.bin"); }
          digitalWrite(SDCARD_CS, HIGH);
          Serial.println("OTA: Upload aborted");
        }
      }
    );

    _otaServer->begin();
    Serial.println("OTA: Web server started on port 80");
    _otaPhase = OTA_PHASE_WAITING;
  }

  void stopOTA() {
    if (_otaServer) { _otaServer->stop(); delete _otaServer; _otaServer = nullptr; }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    _editMode = EDIT_NONE;
    // Resume LoRa radio
    extern void otaResumeRadio();
    otaResumeRadio();
    // Try to restore STA WiFi from saved credentials
    #ifdef MECK_WIFI_COMPANION
    WiFi.mode(WIFI_STA);
    wifiReconnectSaved();
    #endif
    Serial.println("OTA: Stopped, AP down, radio resumed");
  }

  bool verifyFirmwareFile() {
    File f = SD.open("/firmware/update.bin", FILE_READ);
    if (!f) { _otaError = "File not found on SD"; return false; }

    size_t fileSize = f.size();
    if (fileSize < 500000 || fileSize > 6500000) {
      f.close(); digitalWrite(SDCARD_CS, HIGH);
      _otaError = "Bad file size (need 0.5-6MB)";
      Serial.printf("OTA: Bad file size: %d\n", fileSize);
      return false;
    }

    // Check ESP32 image magic byte
    uint8_t magic;
    f.read(&magic, 1);
    f.close();
    digitalWrite(SDCARD_CS, HIGH);

    if (magic != 0xE9) {
      _otaError = "Not a firmware file (bad magic)";
      Serial.printf("OTA: Bad magic: 0x%02X\n", magic);
      return false;
    }
    return true;
  }

  bool flashFirmwareFromSD(DisplayDriver& display) {
    File firmware = SD.open("/firmware/update.bin", FILE_READ);
    if (!firmware) { _otaError = "Cannot open firmware file"; return false; }

    size_t fileSize = firmware.size();
    if (!Update.begin(fileSize, U_FLASH)) {
      _otaError = Update.errorString();
      Serial.printf("OTA: Update.begin failed: %s\n", _otaError);
      firmware.close();
      return false;
    }

    const int BUF_SIZE = 4096;
    uint8_t* buf = (uint8_t*)ps_malloc(BUF_SIZE);
    if (!buf) buf = (uint8_t*)malloc(BUF_SIZE);
    if (!buf) { firmware.close(); Update.abort(); _otaError = "Out of memory"; return false; }

    size_t totalWritten = 0;
    char tmp[48];

    while (firmware.available()) {
      int bytesRead = firmware.read(buf, BUF_SIZE);
      if (bytesRead <= 0) break;

      size_t written = Update.write(buf, bytesRead);
      if (written != (size_t)bytesRead) {
        _otaError = "Flash write error";
        Serial.printf("OTA: Write error at %d bytes\n", totalWritten);
        break;
      }
      totalWritten += written;

      // Update e-ink progress every ~128KB
      if (totalWritten % 131072 < (size_t)BUF_SIZE) {
        display.startFrame();
        display.setColor(DisplayDriver::DARK);
        display.fillRect(2, 14, display.width() - 4, display.height() - 28);
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(2, 14, display.width() - 4, display.height() - 28);
        display.setTextSize(_prefs->smallTextSize());
        display.drawTextCentered(display.width() / 2, 22, "Flashing Firmware");
        snprintf(tmp, sizeof(tmp), "%d / %d KB", (int)(totalWritten / 1024), (int)(fileSize / 1024));
        display.drawTextCentered(display.width() / 2, 42, tmp);
        display.setColor(DisplayDriver::YELLOW);
        display.drawTextCentered(display.width() / 2, 62, "DO NOT POWER OFF");
        display.endFrame();
      }
    }

    free(buf);
    firmware.close();
    digitalWrite(SDCARD_CS, HIGH);

    if (!Update.end(true)) {
      _otaError = Update.errorString();
      Serial.printf("OTA: Update.end failed: %s\n", _otaError);
      return false;
    }

    Serial.printf("OTA: Flash success! %d bytes written\n", totalWritten);
    return true;
  }

  // Called from render loop AND main loop to poll the web server.
  // Handles both OTA firmware upload and SD file manager modes.
  void pollOTAServer() {
    if (_otaServer) {
      if ((_editMode == EDIT_OTA && (_otaPhase == OTA_PHASE_WAITING || _otaPhase == OTA_PHASE_RECEIVING)) ||
          (_editMode == EDIT_FILEMGR && _fmPhase == FM_PHASE_WAITING)) {
        _otaServer->handleClient();
      }
    }
    // Process DNS for captive portal redirect (file manager only)
    if (_dnsServer && _editMode == EDIT_FILEMGR && _fmPhase == FM_PHASE_WAITING) {
      _dnsServer->processNextRequest();
    }
  }

  // Called from main loop — detect upload completion and trigger flash.
  // Must be called from the main loop (not render) because T5S3 FastEPD
  // blocks for 500ms+ per frame, making render-only detection unreliable.
  void checkOTAComplete(DisplayDriver& display) {
    if (_editMode != EDIT_OTA) return;
    if (!_otaUploadOk) return;
    if (_otaPhase != OTA_PHASE_RECEIVING && _otaPhase != OTA_PHASE_WAITING) return;

    Serial.printf("OTA: Upload complete (%d bytes), starting flash sequence\n", _otaBytesReceived);
    processOTAUpload(display);
  }

  // Run the verify → flash → reboot sequence after upload completes
  void processOTAUpload(DisplayDriver& display) {
    // Stop web server and AP first
    if (_otaServer) { _otaServer->stop(); delete _otaServer; _otaServer = nullptr; }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    _otaPhase = OTA_PHASE_VERIFY;
    if (!verifyFirmwareFile()) {
      _otaPhase = OTA_PHASE_ERROR;
      return;
    }

    _otaPhase = OTA_PHASE_FLASH;

    // Backup settings before flashing (preserves identity/contacts across updates)
    extern void backupSettingsToSD();
    backupSettingsToSD();

    if (!flashFirmwareFromSD(display)) {
      _otaPhase = OTA_PHASE_ERROR;
      return;
    }

    _otaPhase = OTA_PHASE_DONE;
    // Show success screen then reboot
    display.startFrame();
    display.setColor(DisplayDriver::DARK);
    display.fillRect(2, 14, display.width() - 4, display.height() - 28);
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(2, 14, display.width() - 4, display.height() - 28);
    display.setTextSize(_prefs->smallTextSize());
    display.setColor(DisplayDriver::GREEN);
    display.drawTextCentered(display.width() / 2, 30, "Update Complete!");
    display.setColor(DisplayDriver::LIGHT);
    File fw = SD.open("/firmware/update.bin", FILE_READ);
    char tmp[48];
    if (fw) {
      snprintf(tmp, sizeof(tmp), "Firmware: %d KB", (int)(fw.size() / 1024));
      fw.close(); digitalWrite(SDCARD_CS, HIGH);
    } else {
      strcpy(tmp, "Firmware written");
    }
    display.drawTextCentered(display.width() / 2, 48, tmp);
    display.drawTextCentered(display.width() / 2, 66, "Rebooting in 3 seconds...");
    display.endFrame();

    delay(3000);
    ESP.restart();
  }

  // ---------------------------------------------------------------------------
  // SD File Manager — WiFi file browser, upload, download, delete
  // ---------------------------------------------------------------------------

  void startFileMgr() {
    _editMode = EDIT_FILEMGR;
    _fmPhase = FM_PHASE_CONFIRM;
    _fmError = nullptr;
  }

  void startFileMgrServer() {
    // Build AP name with last 4 of MAC for uniqueness
    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(_otaApName, sizeof(_otaApName), "Meck-Files-%02X%02X", mac[4], mac[5]);

    // Pause LoRa radio — SD and LoRa share the same SPI bus on both
    // platforms. Incoming packets during SD writes cause bus contention.
    extern void otaPauseRadio();
    otaPauseRadio();

    // Clean WiFi init from any state
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(200);
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_otaApName);
    delay(500);
    Serial.printf("FM: AP '%s' started, IP: %s\n",
                  _otaApName, WiFi.softAPIP().toString().c_str());

    // Start DNS server — redirect ALL DNS lookups to our AP IP.
    // This triggers captive portal detection on phones, which opens the
    // page in a real browser instead of the restricted captive webview.
    if (_dnsServer) { delete _dnsServer; }
    _dnsServer = new DNSServer();
    _dnsServer->start(53, "*", WiFi.softAPIP());
    Serial.println("FM: DNS captive portal started");

    // Start web server
    if (_otaServer) { _otaServer->stop(); delete _otaServer; }
    _otaServer = new WebServer(80);

    // --- Captive portal detection handlers ---
    // Phones/OS probe these URLs to detect captive portals. Redirecting
    // them to our page causes the OS to open a real browser.
    // iOS / macOS
    _otaServer->on("/hotspot-detect.html", HTTP_GET, [this]() {
      Serial.println("FM: captive probe (Apple)");
      _otaServer->sendHeader("Location", "http://192.168.4.1/");
      _otaServer->send(302, "text/plain", "");
    });
    // Android
    _otaServer->on("/generate_204", HTTP_GET, [this]() {
      Serial.println("FM: captive probe (Android)");
      _otaServer->sendHeader("Location", "http://192.168.4.1/");
      _otaServer->send(302, "text/plain", "");
    });
    _otaServer->on("/gen_204", HTTP_GET, [this]() {
      _otaServer->sendHeader("Location", "http://192.168.4.1/");
      _otaServer->send(302, "text/plain", "");
    });
    // Windows
    _otaServer->on("/connecttest.txt", HTTP_GET, [this]() {
      _otaServer->sendHeader("Location", "http://192.168.4.1/");
      _otaServer->send(302, "text/plain", "");
    });
    _otaServer->on("/redirect", HTTP_GET, [this]() {
      _otaServer->sendHeader("Location", "http://192.168.4.1/");
      _otaServer->send(302, "text/plain", "");
    });
    // Firefox
    _otaServer->on("/canonical.html", HTTP_GET, [this]() {
      _otaServer->sendHeader("Location", "http://192.168.4.1/");
      _otaServer->send(302, "text/plain", "");
    });
    _otaServer->on("/success.txt", HTTP_GET, [this]() {
      _otaServer->send(200, "text/plain", "success");
    });

    // --- Main page: server-rendered directory listing (no JS needed) ---
    _otaServer->on("/", HTTP_GET, [this]() {
      String path = _otaServer->arg("path");
      if (path.isEmpty()) path = "/";
      String msg = _otaServer->arg("msg");
      Serial.printf("FM: page request path='%s'\n", path.c_str());
      String html = fmBuildPage(path, msg);
      _otaServer->send(200, "text/html", html);
    });

    // --- File download: GET /dl?path=/file.txt ---
    _otaServer->on("/dl", HTTP_GET, [this]() {
      String path = _otaServer->arg("path");
      File f = SD.open(path, FILE_READ);
      if (!f || f.isDirectory()) {
        if (f) f.close();
        digitalWrite(SDCARD_CS, HIGH);
        _otaServer->send(404, "text/plain", "Not found");
        return;
      }
      String name = path;
      int lastSlash = name.lastIndexOf('/');
      if (lastSlash >= 0) name = name.substring(lastSlash + 1);
      _otaServer->sendHeader("Content-Disposition",
        "attachment; filename=\"" + name + "\"");
      size_t fileSize = f.size();
      _otaServer->setContentLength(fileSize);
      _otaServer->send(200, "application/octet-stream", "");
      uint8_t* buf = (uint8_t*)ps_malloc(4096);
      if (!buf) buf = (uint8_t*)malloc(4096);
      if (buf) {
        while (f.available()) {
          int n = f.read(buf, 4096);
          if (n > 0) _otaServer->sendContent((const char*)buf, n);
        }
        free(buf);
      }
      f.close();
      digitalWrite(SDCARD_CS, HIGH);
    });

    // --- File upload: POST /upload?dir=/ → redirect back to listing ---
    _otaServer->on("/upload", HTTP_POST,
      [this]() {
        String dir = _otaServer->arg("dir");
        if (dir.isEmpty()) dir = "/";
        _otaServer->sendHeader("Location", "/?path=" + dir + "&msg=Upload+complete");
        _otaServer->send(303, "text/plain", "Redirecting...");
      },
      [this]() {
        HTTPUpload& upload = _otaServer->upload();
        static File fmUploadFile;

        if (upload.status == UPLOAD_FILE_START) {
          String dir = _otaServer->arg("dir");
          if (dir.isEmpty()) dir = "/";
          if (!dir.endsWith("/")) dir += "/";
          String fullPath = dir + upload.filename;
          Serial.printf("FM: Upload start: %s\n", fullPath.c_str());
          fmUploadFile = SD.open(fullPath, FILE_WRITE);
          if (!fmUploadFile) Serial.println("FM: Failed to open file for write");

        } else if (upload.status == UPLOAD_FILE_WRITE) {
          if (fmUploadFile) fmUploadFile.write(upload.buf, upload.currentSize);

        } else if (upload.status == UPLOAD_FILE_END) {
          if (fmUploadFile) {
            fmUploadFile.close();
            digitalWrite(SDCARD_CS, HIGH);
            Serial.printf("FM: Upload done: %s (%d bytes)\n",
                          upload.filename.c_str(), upload.totalSize);
          }

        } else if (upload.status == UPLOAD_FILE_ABORTED) {
          if (fmUploadFile) fmUploadFile.close();
          digitalWrite(SDCARD_CS, HIGH);
          Serial.println("FM: Upload aborted");
        }
      }
    );

    // --- Create directory: GET /mkdir?name=xxx&dir=/path ---
    _otaServer->on("/mkdir", HTTP_GET, [this]() {
      String dir = _otaServer->arg("dir");
      String name = _otaServer->arg("name");
      if (dir.isEmpty()) dir = "/";
      if (name.isEmpty()) {
        _otaServer->sendHeader("Location", "/?path=" + dir + "&msg=No+name");
        _otaServer->send(303);
        return;
      }
      String full = dir + (dir.endsWith("/") ? "" : "/") + name;
      bool ok = SD.mkdir(full);
      digitalWrite(SDCARD_CS, HIGH);
      Serial.printf("FM: mkdir '%s' %s\n", full.c_str(), ok ? "OK" : "FAIL");
      _otaServer->sendHeader("Location",
        "/?path=" + dir + "&msg=" + (ok ? "Folder+created" : "mkdir+failed"));
      _otaServer->send(303);
    });

    // --- Delete file/folder: GET /rm?path=/file&ret=/parent ---
    _otaServer->on("/rm", HTTP_GET, [this]() {
      String path = _otaServer->arg("path");
      String ret = _otaServer->arg("ret");
      if (ret.isEmpty()) ret = "/";
      if (path.isEmpty() || path == "/") {
        _otaServer->sendHeader("Location", "/?path=" + ret + "&msg=Bad+path");
        _otaServer->send(303);
        return;
      }
      File f = SD.open(path);
      bool ok = false;
      if (f) {
        bool isDir = f.isDirectory();
        f.close();
        ok = isDir ? SD.rmdir(path) : SD.remove(path);
      }
      digitalWrite(SDCARD_CS, HIGH);
      Serial.printf("FM: rm '%s' %s\n", path.c_str(), ok ? "OK" : "FAIL");
      _otaServer->sendHeader("Location",
        "/?path=" + ret + "&msg=" + (ok ? "Deleted" : "Delete+failed"));
      _otaServer->send(303);
    });

    // --- Confirm delete page: GET /confirm-rm?path=/file&ret=/parent ---
    _otaServer->on("/confirm-rm", HTTP_GET, [this]() {
      String path = _otaServer->arg("path");
      String ret = _otaServer->arg("ret");
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
      _otaServer->send(200, "text/html", html);
    });

    // Catch-all: redirect unknown URLs to file manager (catches captive portal probes)
    _otaServer->onNotFound([this]() {
      Serial.printf("FM: redirect %s -> /\n", _otaServer->uri().c_str());
      _otaServer->sendHeader("Location", "http://192.168.4.1/");
      _otaServer->send(302, "text/plain", "");
    });

    _otaServer->begin();
    Serial.println("FM: Web server started on port 80");
    _fmPhase = FM_PHASE_WAITING;
  }

  void stopFileMgr() {
    if (_otaServer) { _otaServer->stop(); delete _otaServer; _otaServer = nullptr; }
    if (_dnsServer) { _dnsServer->stop(); delete _dnsServer; _dnsServer = nullptr; }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    _editMode = EDIT_NONE;
    extern void otaResumeRadio();
    otaResumeRadio();
    #ifdef MECK_WIFI_COMPANION
    WiFi.mode(WIFI_STA);
    wifiReconnectSaved();
    #endif
    Serial.println("FM: Stopped, AP down, radio resumed");
  }

  // --- Helpers for server-rendered HTML ---

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

  // Build the complete HTML page with inline directory listing
  String fmBuildPage(const String& path, const String& msg) {
    String html;
    html.reserve(4096);

    // --- Head + CSS ---
    html += "<!DOCTYPE html><html><head>"
      "<meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Meck SD Files</title>"
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
    html += "<h1>Meck SD File Manager</h1>";
    html += "<div class='pa'>" + fmHtmlEscape(path) + "</div>";

    // --- Status message (from redirects) ---
    if (msg.length() > 0) {
      html += "<div class='ms'>" + fmHtmlEscape(msg) + "</div>";
    }

    // --- Navigation buttons ---
    html += "<div class='tb'>";
    if (path != "/") {
      // Compute parent
      String parent = path;
      if (parent.endsWith("/")) parent = parent.substring(0, parent.length() - 1);
      int sl = parent.lastIndexOf('/');
      parent = (sl <= 0) ? "/" : parent.substring(0, sl);
      html += "<a class='b' href='/?path=" + fmUrlEncode(parent) + "'>.. Up</a>";
    }
    html += "<a class='b' href='/?path=" + fmUrlEncode(path) + "'>Refresh</a>";
    html += "</div>";

    // --- Directory listing (server-rendered) ---
    File dir = SD.open(path, FILE_READ);
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      digitalWrite(SDCARD_CS, HIGH);
      html += "<div class='em'>Cannot open directory</div>";
    } else {
      // Collect entries into arrays for sorting (dirs first, then alpha)
      struct FmEntry { String name; size_t size; bool isDir; };
      FmEntry entries[128];  // max entries to display
      int count = 0;
      File entry = dir.openNextFile();
      while (entry && count < 128) {
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
      digitalWrite(SDCARD_CS, HIGH);

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
      }
    }

    // --- Upload form (standard HTML form, no JS needed) ---
    html += "<div class='up'>"
      "<form method='POST' action='/upload?dir=" + fmUrlEncode(path) + "' enctype='multipart/form-data'>"
      "<p>Select files to upload</p>"
      "<input type='file' name='file' multiple><br><br>"
      "<button class='b' type='submit'>Upload</button>"
      "</form></div>";

    // --- New folder (tiny inline form) ---
    html += "<form action='/mkdir' method='GET' style='margin:8px 0;display:flex;gap:6px'>"
      "<input type='hidden' name='dir' value='" + fmHtmlEscape(path) + "'>"
      "<input type='text' name='name' placeholder='New folder name' "
      "style='flex:1;padding:7px;border-radius:5px;border:1px solid #4ecca3;"
      "background:#16213e;color:#e0e0e0'>"
      "<button class='b' type='submit'>Create</button>"
      "</form>";

    html += "</body></html>";
    return html;
  }

  #endif

  // ---------------------------------------------------------------------------
  // Edit mode starters
  // ---------------------------------------------------------------------------

  void startEditText(const char* initial) {
    _editMode = EDIT_TEXT;
    strncpy(_editBuf, initial, SETTINGS_TEXT_BUF - 1);
    _editBuf[SETTINGS_TEXT_BUF - 1] = '\0';
    _editPos = strlen(_editBuf);
  }

  void startEditPicker(int initialIdx) {
    _editMode = EDIT_PICKER;
    _editPickerIdx = initialIdx;
  }

  void startEditFloat(float initial) {
    _editMode = EDIT_NUMBER;
    _editFloat = initial;
  }

  void startEditInt(int initial) {
    _editMode = EDIT_NUMBER;
    _editInt = initial;
  }

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------

  int render(DisplayDriver& display) override {
    char tmp[64];

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    if (_onboarding) {
      display.print("Welcome! Setup");
    } else if (_subScreen == SUB_CONTACTS) {
      display.print("Settings > Contacts");
    } else if (_subScreen == SUB_CHANNELS) {
      display.print("Settings > Channels");
    #ifdef MECK_OTA_UPDATE
    } else if (_subScreen == SUB_OTA_TOOLS) {
      display.print("Settings > OTA Tools");
    #endif
    } else {
      display.print("Settings");
    }

    // (Row indicator is now a scrollbar on the right of the list body.)

    display.drawRect(0, 11, display.width(), 1);

    // === Body ===
    display.setTextSize(_prefs->smallTextSize());  // tiny font
    int lineHeight = _prefs->smallLineH();
    int headerH = 14;
    int footerH = 14;
    int maxY = display.height() - footerH;
    _renderScreenH = display.height();  // captured for selectRowAtVY() touch hit-testing

    int maxVisible = (maxY - headerH) / lineHeight;
    if (maxVisible < 3) maxVisible = 3;
    // _scrollTop is maintained independently: ensureCursorVisible() on cursor
    // moves, pageScroll() on swipe. Clamp to range here so a swipe can scroll
    // the view without dragging the selection along.
    if (_numRows <= maxVisible) _scrollTop = 0;
    else _scrollTop = max(0, min(_scrollTop, _numRows - maxVisible));
    int endIdx = min(_numRows, _scrollTop + maxVisible);

    // Scrollbar: always shown on the top-level list; on sub-screens only when
    // the rows overflow the page. When shown, reserve a few px on the right so
    // the selection highlight isn't painted under it.
    bool showScrollbar = (_subScreen == SUB_NONE) || (_numRows > maxVisible);
    int sbW = showScrollbar ? 6 : 0;

    int y = headerH;

#if defined(MECK_TWATCH)
    // Watch: rows longer than the 120px line (e.g. "GPS Baud: Default (9600)")
    // must clip at the screen edge, not wrap onto the row below and overprint
    // it. Restored after the loop -- other screens rely on wrap being on.
    ((LGFXDisplay*)&display)->setTextWrap(false);
    _tickerActive = false;
#endif

    for (int i = _scrollTop; i < endIdx && y + lineHeight <= maxY; i++) {
      bool selected = (i == _cursor);
      bool editing = selected && (_editMode != EDIT_NONE);

      // Selection highlight
      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y + _prefs->smallHighlightOff(), display.width() - sbW, lineHeight);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

#if defined(MECK_TWATCH)
      // Selected-row ticker, mirroring TWatchChannelScreen::drawTicker. The
      // row text is only known inside the switch below, so the marquee's
      // seamless second copy is drawn by running the row's case twice, one
      // period apart. Width is measured on pass 0 via getCursorX, so a newly
      // selected row shows its first frame unscrolled and scrolls from the
      // next frame on.
      int rowStartX = 0;
      int passes = 1;
      if (selected) {
        if (_tickerRow != i) { _tickerRow = i; _tickerStartMs = millis(); _tickerTextW = 0; }
        if (_tickerTextW > display.width() - sbW - 4) {
          int period = _tickerTextW + 24;
          int off = (int)(((millis() - _tickerStartMs) / SETTINGS_TICKER_MS_PER_PX) % (unsigned long)period);
          rowStartX = 2 - off;
          passes = 2;
          _tickerActive = true;
        }
      }
      int rowTextW = 0;
      for (int tickerPass = 0; tickerPass < passes; tickerPass++) {
      display.setCursor(rowStartX + tickerPass * (_tickerTextW + 24), y);
#else
      display.setCursor(0, y);
#endif

      switch (_rows[i].type) {
        case ROW_NAME:
          if (editing && _editMode == EDIT_TEXT) {
            snprintf(tmp, sizeof(tmp), "Name: %s_", _editBuf);
            display.print(tmp);
          } else {
#if defined(MECK_TWATCH)
            // Watch: draw the raw name so any known emoji renders inline as a
            // 10x10 colour image (an app-imported name like "<seedling> Ivy"
            // otherwise prints its UTF-8 bytes as garbage glyphs). Starts at
            // the pass cursor so the selected-row marquee still works. tmp is
            // left holding the emoji-free text for the width measurement below,
            // with the emoji cells' width added there.
            {
              LGFXDisplay& d = (LGFXDisplay&)display;
              int rx = d.getCursorX();
              d.setCursor(rx, y);
              display.print("Name: ");
              rx += display.getTextWidth("Name: ");
              watchEmojiDrawRun(d, rx, y, _prefs->node_name);
              // emoji-free copy for the ticker's width measurement
              char plain[sizeof(_prefs->node_name)];
              display.translateUTF8ToBlocks(plain, _prefs->node_name, sizeof(plain));
              snprintf(tmp, sizeof(tmp), "Name: %s", plain);
              _nameEmojiW = watchEmojiTextWidth(d, _prefs->node_name) - display.getTextWidth(plain);
            }
#else
            snprintf(tmp, sizeof(tmp), "Name: %s", _prefs->node_name);
            display.print(tmp);
#endif
          }
          break;

        case ROW_RADIO_PRESET: {
          int preset = detectCurrentPreset();
          if (editing && _editMode == EDIT_PICKER) {
            if (_editPickerIdx >= 0 && _editPickerIdx < (int)NUM_RADIO_PRESETS) {
              snprintf(tmp, sizeof(tmp), "< %s >", RADIO_PRESETS[_editPickerIdx].name);
            } else {
              strcpy(tmp, "< Custom >");
            }
          } else {
            if (preset >= 0) {
              snprintf(tmp, sizeof(tmp), "Preset: %s", RADIO_PRESETS[preset].name);
            } else {
              strcpy(tmp, "Preset: Custom");
            }
          }
          display.print(tmp);
          break;
        }

        case ROW_FREQ:
          if (editing && _editMode == EDIT_TEXT) {
            snprintf(tmp, sizeof(tmp), "Freq: %s_ MHz", _editBuf);
          } else {
            snprintf(tmp, sizeof(tmp), "Freq: %.3f MHz", _prefs->freq);
          }
          display.print(tmp);
          break;

        case ROW_BW:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "BW: %.1f " EDIT_ADJ_HINT, _editFloat);
          } else {
            snprintf(tmp, sizeof(tmp), "BW: %.1f kHz", _prefs->bw);
          }
          display.print(tmp);
          break;

        case ROW_SF:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "SF: %d " EDIT_ADJ_HINT, _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "SF: %d", _prefs->sf);
          }
          display.print(tmp);
          break;

        case ROW_CR:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "CR: %d " EDIT_ADJ_HINT, _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "CR: %d", _prefs->cr);
          }
          display.print(tmp);
          break;

        case ROW_TX_POWER:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "TX: %d dBm " EDIT_ADJ_HINT, _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "TX: %d dBm", _prefs->tx_power_dbm);
          }
          display.print(tmp);
          break;

        case ROW_UTC_OFFSET:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "UTC: %+d " EDIT_ADJ_HINT, _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "UTC Offset: %+d", _prefs->utc_offset_hours);
          }
          display.print(tmp);
          break;

        case ROW_BACKLIGHT_BRIGHTNESS:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "Brightness: %d%% " EDIT_ADJ_HINT, _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "Backlight Brightness: %d%%", _prefs->backlight_brightness_pct);
          }
          display.print(tmp);
          break;

        case ROW_KB_BACKLIGHT:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "Keyboard LED: %d%% " EDIT_ADJ_HINT, _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "Keyboard LED: %d%%", _prefs->kb_backlight_pct);
          }
          display.print(tmp);
          break;

        case ROW_MSG_NOTIFY:
          snprintf(tmp, sizeof(tmp), "Msg LED Flash: %s",
                   _prefs->kb_flash_notify ? "ON" : "OFF");
          display.print(tmp);
          break;

        case ROW_PATH_HASH_SIZE:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "Path Hash Size: %d-byte " EDIT_ADJ_HINT, _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "Path Hash Size: %d-byte", _prefs->path_hash_mode + 1);
          }
          display.print(tmp);
          break;

        case ROW_DEFAULT_SCOPE:
          if (editing && _editMode == EDIT_TEXT) {
            snprintf(tmp, sizeof(tmp), "Region: %s_", _editBuf);
          } else if (_prefs->default_scope_name[0]) {
            snprintf(tmp, sizeof(tmp), "Default Region: %s", _prefs->default_scope_name);
          } else {
            strcpy(tmp, "Default Region: (none)");
          }
          display.print(tmp);
          break;

        case ROW_GPS_BAUD: {
          char baudStr[16];
          if (editing && _editMode == EDIT_PICKER) {
            snprintf(tmp, sizeof(tmp), "< GPS Baud: %s > *",
                     gpsBaudLabel(GPS_BAUD_OPTIONS[_editPickerIdx], baudStr, sizeof(baudStr)));
          } else {
            snprintf(tmp, sizeof(tmp), "GPS Baud: %s *",
                     gpsBaudLabel(_prefs->gps_baudrate, baudStr, sizeof(baudStr)));
          }
          display.print(tmp);
          break;
        }


        case ROW_DARK_MODE:
          snprintf(tmp, sizeof(tmp), "Dark Mode: %s",
                   _prefs->dark_mode ? "ON" : "OFF");
          display.print(tmp);
          break;

        case ROW_LARGE_FONT:
          snprintf(tmp, sizeof(tmp), "Font Size: %s",
                   _prefs->large_font ? "LARGER" : "TINY");
          display.print(tmp);
          break;

        case ROW_FONT_STYLE:
          if (editing && _editMode == EDIT_PICKER) {
            snprintf(tmp, sizeof(tmp), "< Font: %s >",
                     meckFontStyleName(_prefs->ui_font_style));
          } else {
            snprintf(tmp, sizeof(tmp), "Font: %s",
                     meckFontStyleName(_prefs->ui_font_style));
          }
          display.print(tmp);
          break;



        #ifdef MECK_WIFI_COMPANION
        case ROW_WIFI_SETUP:
          if (WiFi.status() == WL_CONNECTED) {
            snprintf(tmp, sizeof(tmp), "WiFi: %s", WiFi.SSID().c_str());
          } else {
            strcpy(tmp, "WiFi: (not connected)");
          }
          display.print(tmp);
          break;
        case ROW_WIFI_TOGGLE:
          snprintf(tmp, sizeof(tmp), "WiFi Radio: %s",
                   (WiFi.getMode() != WIFI_OFF) ? "ON" : "OFF");
          display.print(tmp);
          break;
        #endif


        // --- Submenu folder rows ---
        case ROW_CONTACTS_SUBMENU:
          display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
          display.print("Contacts >>");
          break;

        case ROW_CHANNELS_SUBMENU:
          display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
          display.print("Channels >>");
          break;

        case ROW_RXLOG:
          display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
          display.print("Rx Log >>");
          break;

#if defined(MECK_TWATCH)
        case ROW_FILE_MANAGER:
          display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
          if (wfmActive()) {
            snprintf(tmp, sizeof(tmp), "File Mgr: %s", wfmSSID());
            display.print(tmp);
          } else {
            display.print("File Manager: Off");
          }
          break;
#endif

#if defined(MECK_TWATCH)
        case ROW_EXPERIMENTAL_SUBMENU:
          display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
          display.print("Experimental Features >>");
          break;

        case ROW_CANNED_SUBMENU:
          display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
          display.print("Canned Messages >>");
          break;

        case ROW_CANNED_SLOT: {
          uint8_t slot = _rows[i].param;
          const char* msg = _prefs->canned_msgs[slot];
          if (msg[0]) {
            snprintf(tmp, sizeof(tmp), "%u: %.28s", (unsigned)(slot + 1), msg);
          } else {
            snprintf(tmp, sizeof(tmp), "%u: (empty)", (unsigned)(slot + 1));
          }
          display.print(tmp);
          break;
        }

        case ROW_RX_DUTY_CYCLE:
          snprintf(tmp, sizeof(tmp), "Rx Duty Cycle: %s",
                   _prefs->rx_duty_cycle ? "ON" : "OFF");
          display.print(tmp);
          break;

        case ROW_POSITION_MODE: {
          const char* pm = (_prefs->position_mode == 2) ? "Auto (GPS)"
                         : (_prefs->position_mode == 1) ? "Manual" : "Off";
          snprintf(tmp, sizeof(tmp), "Position: %s", pm);
          display.print(tmp);
          break;
        }
        case ROW_LATITUDE:
          if (editing && _editMode == EDIT_TEXT) {
            snprintf(tmp, sizeof(tmp), "Lat: %s_", _editBuf);
          } else {
            snprintf(tmp, sizeof(tmp), "Lat: %.6f%s", positionLat(),
                     _prefs->position_mode == 2 ? " (GPS)" : "");
          }
          display.print(tmp);
          break;
        case ROW_LONGITUDE:
          if (editing && _editMode == EDIT_TEXT) {
            snprintf(tmp, sizeof(tmp), "Lon: %s_", _editBuf);
          } else {
            snprintf(tmp, sizeof(tmp), "Lon: %.6f%s", positionLon(),
                     _prefs->position_mode == 2 ? " (GPS)" : "");
          }
          display.print(tmp);
          break;
        case ROW_RX_DUTY_LEVEL: {
          const char* lvl = (_prefs->rx_duty_level == 2) ? "Aggressive"
                          : (_prefs->rx_duty_level == 1) ? "Medium" : "Light";
          snprintf(tmp, sizeof(tmp), "Nap Depth: %s", lvl);
          display.print(tmp);
          break;
        }

        case ROW_RX_DUTY_UNAVAIL:
          display.setColor(DisplayDriver::YELLOW);
          display.print("Rx Duty: SF9 or lower only");
          break;
#endif

        #ifdef MECK_HAS_CONFIG_EXPORT
        case ROW_EXPORT_IMPORT_SUBMENU:
          display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
          display.print("Export/Import >>");
          break;

        case ROW_EXPORT_TO_SD:
          display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
#if defined(MECK_TWATCH)
          display.print("Export to storage >>");
#else
          display.print("Export to SD >>");
#endif
          break;

        case ROW_IMPORT_FROM_SD:
#if defined(MECK_TWATCH)
          display.print("Import from storage");
#else
          display.print("Import from SD");
#endif
          break;

        case ROW_EXPORT_IDENTITY:
          snprintf(tmp, sizeof(tmp), "  [%c] Identity",
                   (_exportFlags & MECK_EXPORT_IDENTITY) ? 'X' : ' ');
          display.print(tmp);
          break;

        case ROW_EXPORT_RADIO:
          snprintf(tmp, sizeof(tmp), "  [%c] Radio Settings",
                   (_exportFlags & MECK_EXPORT_RADIO) ? 'X' : ' ');
          display.print(tmp);
          break;

        case ROW_EXPORT_CHANNELS:
          snprintf(tmp, sizeof(tmp), "  [%c] Channels",
                   (_exportFlags & MECK_EXPORT_CHANNELS) ? 'X' : ' ');
          display.print(tmp);
          break;

        case ROW_EXPORT_CONTACTS:
          snprintf(tmp, sizeof(tmp), "  [%c] Contacts",
                   (_exportFlags & MECK_EXPORT_CONTACTS) ? 'X' : ' ');
          display.print(tmp);
          break;

        case ROW_EXPORT_AUTOADD:
          snprintf(tmp, sizeof(tmp), "    [%c] Auto-Add Prefs",
                   (_exportFlags & MECK_EXPORT_AUTOADD) ? 'X' : ' ');
          display.print(tmp);
          break;

        case ROW_EXPORT_NOW:
          display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
          display.print(">> Export Now");
          break;
        #endif

        // --- Contacts section ---
        case ROW_CONTACT_HEADER:
          display.setColor(DisplayDriver::YELLOW);
          display.print("--- Contacts ---");
          break;

        case ROW_CONTACT_MODE:
          if (editing && _editMode == EDIT_PICKER) {
            snprintf(tmp, sizeof(tmp), "< Add Mode: %s >",
                     contactModeLabel(_editPickerIdx));
          } else {
            snprintf(tmp, sizeof(tmp), "Add Mode: %s",
                     contactModeLabel(getContactMode()));
          }
          display.print(tmp);
          break;

        case ROW_AUTOADD_CHAT:
          snprintf(tmp, sizeof(tmp), "  Companion: %s",
                   (_prefs->autoadd_config & AUTO_ADD_CHAT) ? "ON" : "OFF");
          display.print(tmp);
          break;

        case ROW_AUTOADD_REPEATER:
          snprintf(tmp, sizeof(tmp), "  Repeater: %s",
                   (_prefs->autoadd_config & AUTO_ADD_REPEATER) ? "ON" : "OFF");
          display.print(tmp);
          break;

        case ROW_AUTOADD_ROOM:
          snprintf(tmp, sizeof(tmp), "  Room Server: %s",
                   (_prefs->autoadd_config & AUTO_ADD_ROOM_SERVER) ? "ON" : "OFF");
          display.print(tmp);
          break;

        case ROW_AUTOADD_SENSOR:
          snprintf(tmp, sizeof(tmp), "  Sensor: %s",
                   (_prefs->autoadd_config & AUTO_ADD_SENSOR) ? "ON" : "OFF");
          display.print(tmp);
          break;

        case ROW_AUTOADD_OVERWRITE:
          snprintf(tmp, sizeof(tmp), "  Overwrite Oldest: %s",
                   (_prefs->autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) ? "ON" : "OFF");
          display.print(tmp);
          break;

        // --- Channels section ---
        case ROW_CH_HEADER:
          display.setColor(DisplayDriver::YELLOW);
          display.print("--- Channels ---");
          break;

        case ROW_CHANNEL: {
          uint8_t chIdx = _rows[i].param;
          ChannelDetails ch;
          if (the_mesh.getChannel(chIdx, ch)) {
            if (editing && _editMode == EDIT_TEXT) {
              // Editing scope for this channel
              snprintf(tmp, sizeof(tmp), " %s [%s_]", ch.name, _editBuf);
            } else {
              // Show channel name + scope tag
              if (ch.scope_name[0]) {
                snprintf(tmp, sizeof(tmp), " %s [%s]", ch.name, ch.scope_name);
              } else {
                snprintf(tmp, sizeof(tmp), " %s [*]", ch.name);
              }
              if (selected) {
                // Build hint with notification state + actions
                uint8_t nPref = _prefs->channel_notif[chIdx];
                const char* nTag = (nPref == NOTIF_NONE) ? "Off" :
                                   (nPref == NOTIF_MENTIONS) ? "@" : "All";
                char hintBuf[40];
                if (chIdx > 0) {
                  snprintf(hintBuf, sizeof(hintBuf), "N:%s Ent:Region X:Del", nTag);
                } else {
                  snprintf(hintBuf, sizeof(hintBuf), "N:%s Ent:Region", nTag);
                }
                int hintW = display.getTextWidth(hintBuf);
                display.setCursor(display.width() - hintW - 2, y);
                display.print(hintBuf);
                display.setCursor(0, y);
              }
            }
          } else {
            snprintf(tmp, sizeof(tmp), " (empty)");
          }
          display.print(tmp);
          break;
        }

        case ROW_ADD_CHANNEL:
          if (editing && _editMode == EDIT_TEXT) {
            snprintf(tmp, sizeof(tmp), "> %s_", _editBuf);
          } else {
            display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
            strcpy(tmp, "+ Add Channel (# = public)");
          }
          display.print(tmp);
          break;

        #ifdef MECK_OTA_UPDATE
        case ROW_OTA_TOOLS_SUBMENU:
          display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
          display.print("OTA Tools >>");
          break;

        case ROW_FW_UPDATE:
          display.print("Firmware Update");
          break;

        case ROW_SD_FILE_MGR:
          display.print("SD File Manager");
          break;
        #endif

        case ROW_INFO_HEADER:
          display.setColor(DisplayDriver::YELLOW);
          display.print("--- Device Info ---");
          break;

        case ROW_PUB_KEY: {
          // Show first 8 bytes of pub key as hex (16 chars)
          char hexBuf[17];
          mesh::Utils::toHex(hexBuf, the_mesh.self_id.pub_key, 8);
          snprintf(tmp, sizeof(tmp), "Node ID: %s", hexBuf);
          display.print(tmp);
          break;
        }

        case ROW_FIRMWARE:
          snprintf(tmp, sizeof(tmp), "FW: %s", FIRMWARE_VERSION);
          display.print(tmp);
          break;

        case ROW_CLIENT_REPEAT:
          if (the_mesh.isValidClientRepeatFreq((uint32_t)(_prefs->freq * 1000.0f + 0.5f))) {
            snprintf(tmp, sizeof(tmp), "Client Repeat: %s", _prefs->isRepeatEn() ? "On" : "Off");
          } else {
            snprintf(tmp, sizeof(tmp), "Client Repeat: n/a (freq)");
          }
          display.print(tmp);
          break;

#if defined(MECK_TWATCH)
        case ROW_MCU_TEMP:
          // ESP32-S3 on-die sensor via ESP32Board::getMCUTemperature() (whole degrees);
          // the same value the node reports in its self telemetry (MCU temp, PR #2897).
          snprintf(tmp, sizeof(tmp), "MCU Temp: %d C", (int)board.getMCUTemperature());
          display.print(tmp);
          break;
#endif

      }

#if defined(MECK_TWATCH)
      if (selected && tickerPass == 0) {
        // getCursorX() is unusable here: LGFXDisplay::print() calls buffer.println(),
        // whose newline resets the cursor X to 0. Measure the string directly.
        rowTextW = display.getTextWidth(tmp);
        // The Name row draws its emoji as 10x10 cells that getTextWidth() can't
        // see; add their width so the marquee period covers the whole row.
        if (_rows[i].type == ROW_NAME) rowTextW += _nameEmojiW;
      }
      }  // ticker pass loop
      if (selected) _tickerTextW = rowTextW;
#endif

      y += lineHeight;
    }

#if defined(MECK_TWATCH)
    ((LGFXDisplay*)&display)->setTextWrap(true);
#endif

    // Scrollbar (track + proportional thumb), mirroring the notif-sound picker.
    if (showScrollbar) {
      int sbX = display.width() - 6;
      int sbTop = headerH;
      int sbH = maxY - headerH;
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(sbX, sbTop, 3, sbH);
      int thumbH = max(4, (maxVisible * sbH) / _numRows);
      if (thumbH > sbH) thumbH = sbH;
      int maxScroll = _numRows - maxVisible;
      if (maxScroll < 1) maxScroll = 1;
      int thumbY = sbTop + (_scrollTop * (sbH - thumbH)) / maxScroll;
      display.fillRect(sbX + 1, thumbY + 1, 1, thumbH - 2);
    }

    display.setTextSize(1);

    // === Confirmation overlay ===
    if (_editMode == EDIT_CONFIRM) {
      int bx = 4, by = 30, bw = display.width() - 8, bh = 36;
      display.setColor(DisplayDriver::DARK);
      display.fillRect(bx, by, bw, bh);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(bx, by, bw, bh);

      display.setTextSize(_prefs->smallTextSize());
      if (_confirmAction == 1) {
        uint8_t chIdx = _rows[_cursor].param;
        ChannelDetails ch;
        the_mesh.getChannel(chIdx, ch);
        snprintf(tmp, sizeof(tmp), "Delete %s?", ch.name);
        display.drawTextCentered(display.width() / 2, by + 4, tmp);
      } else if (_confirmAction == 2) {
        display.drawTextCentered(display.width() / 2, by + 4, "Apply radio changes?");
      } else if (_confirmAction == 3) {
        display.drawTextCentered(display.width() / 2, by + 4, "Region not set.");
        display.drawTextCentered(display.width() / 2, by + 15, "Leave unset?");
      }
      display.drawTextCentered(display.width() / 2, by + bh - 14, "Enter:Yes  Q:No");
      display.setTextSize(1);
    }

    // === Notification sound picker overlay ===

    #ifdef MECK_WIFI_COMPANION
    // === WiFi setup overlay ===
    if (_editMode == EDIT_WIFI) {
      int bx = 2, by = 14, bw = display.width() - 4;
      int bh = display.height() - 28;
      display.setColor(DisplayDriver::DARK);
      display.fillRect(bx, by, bw, bh);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(bx, by, bw, bh);

      display.setTextSize(_prefs->smallTextSize());
      int wy = by + 4;

      if (_wifiPhase == WIFI_PHASE_SCANNING) {
        display.drawTextCentered(display.width() / 2, wy, "Scanning for networks...");

      } else if (_wifiPhase == WIFI_PHASE_SELECT) {
        if (_wifiSSIDCount == 0) {
          // No networks found — show message with rescan prompt
          display.setCursor(bx + 4, wy);
          display.print("No networks found.");
          wy += 12;
          display.setCursor(bx + 4, wy);
          display.print("Check your hotspot is on");
          wy += 8;
          display.setCursor(bx + 4, wy);
          display.print("and set to 2.4GHz.");
          wy += 12;
          display.setCursor(bx + 4, wy);
          display.print("Press R or Enter to rescan.");
        } else {
        display.setCursor(bx + 4, wy);
        display.print("Select network:");
        wy += 10;
        for (int wi = 0; wi < _wifiSSIDCount && wy < by + bh - 16; wi++) {
          bool sel = (wi == _wifiSSIDSelected);
          if (sel) {
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(bx + 2, wy + _prefs->smallHighlightOff(), bw - 4, 8);
            display.setColor(DisplayDriver::DARK);
          } else {
            display.setColor(DisplayDriver::LIGHT);
          }
          // Prefix marker, then the SSID ellipsized to fit the box interior so
          // long names don't overflow the overlay border (no clip-rect available).
          const char* prefix = sel ? "> " : "  ";
          display.setCursor(bx + 4, wy);
          display.print(prefix);
          int nameX = bx + 4 + display.getTextWidth(prefix);
          int nameMaxW = (bx + bw - 2) - nameX;
          display.drawTextEllipsized(nameX, wy, nameMaxW, _wifiSSIDs[wi].c_str());
          wy += 8;
        }
        }

      } else if (_wifiPhase == WIFI_PHASE_PASSWORD) {
        display.setCursor(bx + 4, wy);
        snprintf(tmp, sizeof(tmp), "SSID: %s", _wifiSSIDs[_wifiSSIDSelected].c_str());
        display.print(tmp);
        wy += 12;
        display.setCursor(bx + 4, wy);
        display.print("Password:");
        wy += 10;
        display.setCursor(bx + 4, wy);
        // Masked password with brief reveal of last char
        char passBuf[66];
        for (int pi = 0; pi < _wifiPassLen; pi++) passBuf[pi] = '*';
        if (_wifiPassLen > 0 && _wifiFormLastChar > 0 &&
            (millis() - _wifiFormLastChar) < 800) {
          passBuf[_wifiPassLen - 1] = _wifiPassBuf[_wifiPassLen - 1];
        }
        passBuf[_wifiPassLen] = '_';
        passBuf[_wifiPassLen + 1] = '\0';
        display.print(passBuf);

      } else if (_wifiPhase == WIFI_PHASE_CONNECTING) {
        display.drawTextCentered(display.width() / 2, wy + 10, "Connecting...");
      }
      display.setTextSize(1);
    }
    #endif

    #ifdef MECK_OTA_UPDATE
    // === OTA update overlay ===
    if (_editMode == EDIT_OTA) {
      int bx = 2, by = 14, bw = display.width() - 4;
      int bh = display.height() - 28;
      display.setColor(DisplayDriver::DARK);
      display.fillRect(bx, by, bw, bh);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(bx, by, bw, bh);

      display.setTextSize(_prefs->smallTextSize());
      int oy = by + 4;

      if (_otaPhase == OTA_PHASE_CONFIRM) {
        display.drawTextCentered(display.width() / 2, oy, "Firmware Update");
        oy += 14;
        display.setCursor(bx + 4, oy);
        display.print("Start WiFi upload server?");
        oy += 10;
        display.setCursor(bx + 4, oy);
        display.print("You will upload a .bin file");
        oy += 8;
        display.setCursor(bx + 4, oy);
        display.print("from your device's browser.");

      } else if (_otaPhase == OTA_PHASE_AP_START) {
        display.drawTextCentered(display.width() / 2, oy + 20, "Starting WiFi...");

      } else if (_otaPhase == OTA_PHASE_WAITING) {
        display.drawTextCentered(display.width() / 2, oy, "Firmware Update");
        oy += 14;
        display.setCursor(bx + 4, oy);
        display.print("Connect to WiFi network:");
        oy += 10;
        display.setColor(DisplayDriver::GREEN);
        display.setCursor(bx + 4, oy);
        display.print(_otaApName);
        display.setColor(DisplayDriver::LIGHT);
        oy += 12;
        display.setCursor(bx + 4, oy);
        display.print("Then open browser:");
        oy += 10;
        display.setColor(DisplayDriver::GREEN);
        display.setCursor(bx + 4, oy);
        char ipBuf[32];
        snprintf(ipBuf, sizeof(ipBuf), "http://%s", WiFi.softAPIP().toString().c_str());
        display.print(ipBuf);
        display.setColor(DisplayDriver::LIGHT);
        oy += 12;
        display.setCursor(bx + 4, oy);
        display.print("Waiting for upload...");

        // Poll the web server during render
        pollOTAServer();

      } else if (_otaPhase == OTA_PHASE_RECEIVING) {
        display.drawTextCentered(display.width() / 2, oy, "Receiving Firmware");
        oy += 16;
        char progBuf[32];
        snprintf(progBuf, sizeof(progBuf), "%d KB received", (int)(_otaBytesReceived / 1024));
        display.drawTextCentered(display.width() / 2, oy, progBuf);
        oy += 14;
        display.setCursor(bx + 4, oy);
        display.print("Do not close browser");

        // Keep polling during receive
        pollOTAServer();

      } else if (_otaPhase == OTA_PHASE_VERIFY) {
        display.drawTextCentered(display.width() / 2, oy + 20, "Verifying file...");

      } else if (_otaPhase == OTA_PHASE_FLASH) {
        display.drawTextCentered(display.width() / 2, oy + 10, "Flashing Firmware");
        display.setColor(DisplayDriver::YELLOW);
        display.drawTextCentered(display.width() / 2, oy + 30, "DO NOT POWER OFF");
        display.setColor(DisplayDriver::LIGHT);

      } else if (_otaPhase == OTA_PHASE_ERROR) {
        display.setColor(DisplayDriver::YELLOW);
        display.drawTextCentered(display.width() / 2, oy, "Update Failed");
        display.setColor(DisplayDriver::LIGHT);
        oy += 14;
        if (_otaError) {
          display.setCursor(bx + 4, oy);
          display.print(_otaError);
        }
      }

      display.setTextSize(1);
    }

    // === File Manager overlay ===
    if (_editMode == EDIT_FILEMGR) {
      int bx = 2, by = 14, bw = display.width() - 4;
      int bh = display.height() - 28;
      display.setColor(DisplayDriver::DARK);
      display.fillRect(bx, by, bw, bh);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(bx, by, bw, bh);

      display.setTextSize(_prefs->smallTextSize());
      int oy = by + 4;

      if (_fmPhase == FM_PHASE_CONFIRM) {
        display.drawTextCentered(display.width() / 2, oy, "SD File Manager");
        oy += 14;
        display.setCursor(bx + 4, oy);
        display.print("Start WiFi file server?");
        oy += 10;
        display.setCursor(bx + 4, oy);
        display.print("Upload and download files");
        oy += 8;
        display.setCursor(bx + 4, oy);
        display.print("on SD card via browser.");
        oy += 10;
        display.setCursor(bx + 4, oy);
        display.setColor(DisplayDriver::YELLOW);
        display.print("LoRa paused while active.");
        display.setColor(DisplayDriver::LIGHT);

      } else if (_fmPhase == FM_PHASE_WAITING) {
        display.drawTextCentered(display.width() / 2, oy, "SD File Manager");
        oy += 14;
        display.setCursor(bx + 4, oy);
        display.print("Connect to WiFi network:");
        oy += 10;
        display.setColor(DisplayDriver::GREEN);
        display.setCursor(bx + 4, oy);
        display.print(_otaApName);
        display.setColor(DisplayDriver::LIGHT);
        oy += 12;
        display.setCursor(bx + 4, oy);
        display.print("Then open browser:");
        oy += 10;
        display.setColor(DisplayDriver::GREEN);
        display.setCursor(bx + 4, oy);
        char ipBuf[32];
        snprintf(ipBuf, sizeof(ipBuf), "http://%s", WiFi.softAPIP().toString().c_str());
        display.print(ipBuf);
        display.setColor(DisplayDriver::LIGHT);
        oy += 12;
        display.setCursor(bx + 4, oy);
        display.print("File server active...");

        pollOTAServer();

      } else if (_fmPhase == FM_PHASE_ERROR) {
        display.setColor(DisplayDriver::YELLOW);
        display.drawTextCentered(display.width() / 2, oy, "File Manager Error");
        display.setColor(DisplayDriver::LIGHT);
        oy += 14;
        if (_fmError) {
          display.setCursor(bx + 4, oy);
          display.print(_fmError);
        }
      }

      display.setTextSize(1);
    }
    #endif

    // === Share contact picker overlay ===
    if (_editMode == EDIT_SHARE_PICK) {
      int bx = 2, by = 14, bw = display.width() - 4;
      int bh = display.height() - 28;
      display.setColor(DisplayDriver::DARK);
      display.fillRect(bx, by, bw, bh);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(bx, by, bw, bh);

      display.setTextSize(_prefs->smallTextSize());
      int lineH = _prefs->smallLineH();

      // Header
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(bx + 4, by + 3);
      display.print("Share with contact:");

      if (_shareContactCount == 0) {
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(bx + 4, by + 16);
        display.print("No contacts available");
      } else {
        int listTop = by + 14;
        int listBot = by + bh - 14;
        int maxVisible = (listBot - listTop) / lineH;
        if (maxVisible < 3) maxVisible = 3;

        // Scroll to keep selection visible
        if (_sharePickerIdx < _sharePickerScroll) _sharePickerScroll = _sharePickerIdx;
        if (_sharePickerIdx >= _sharePickerScroll + maxVisible) _sharePickerScroll = _sharePickerIdx - maxVisible + 1;

        for (int vi = 0; vi < maxVisible && (_sharePickerScroll + vi) < _shareContactCount; vi++) {
          int ci = _sharePickerScroll + vi;
          int iy = listTop + vi * lineH;
          bool sel = (ci == _sharePickerIdx);

          if (sel) {
            display.setColor(DisplayDriver::GREEN);
            display.fillRect(bx + 1, iy, bw - 2, lineH);
            display.setColor(DisplayDriver::DARK);
          } else {
            display.setColor(DisplayDriver::LIGHT);
          }

          ContactInfo ci_info;
          if (the_mesh.getContactByIdx(_shareContacts[ci], ci_info)) {
            display.setCursor(bx + 4, iy + 1);
            if (ci_info.flags & 0x01) {
              display.print("* ");
            }
            display.print(ci_info.name);
          }
        }

        // Scroll indicator
        if (_shareContactCount > maxVisible) {
          int sbX = bx + bw - 4;
          int sbH = listBot - listTop;
          display.setColor(DisplayDriver::LIGHT);
          display.drawRect(sbX, listTop, 3, sbH);
          int thumbH = max(4, (maxVisible * sbH) / _shareContactCount);
          int maxScroll = _shareContactCount - maxVisible;
          if (maxScroll < 1) maxScroll = 1;
          int thumbY = listTop + (_sharePickerScroll * (sbH - thumbH)) / maxScroll;
          display.fillRect(sbX + 1, thumbY + 1, 1, thumbH - 2);
        }
      }

      // Footer hint
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(bx + 4, by + bh - 12);
      display.print("Enter:Send  Q:Cancel");
      display.setTextSize(1);
    }

    // === Footer ===
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);

    if (_editMode == EDIT_TEXT) {
      display.print("Type, Enter:Ok Sh+Del:Cancel");
    #ifdef MECK_WIFI_COMPANION
    } else if (_editMode == EDIT_WIFI) {
      if (_wifiPhase == WIFI_PHASE_SELECT) {
        if (_wifiSSIDCount == 0) {
          display.print("No networks. Button:Rescan");
        } else {
          display.print("Hold:Slct Button:Rescan");
        }
      } else if (_wifiPhase == WIFI_PHASE_PASSWORD) {
        display.print("Enter:Connect Sh+Del:Exit");
      } else {
        display.print("Please wait...");
      }
    #endif
    #ifdef MECK_OTA_UPDATE
    } else if (_editMode == EDIT_OTA) {
      if (_otaPhase == OTA_PHASE_CONFIRM) {
        display.print("Enter:Start  Q:Cancel");
      } else if (_otaPhase == OTA_PHASE_WAITING) {
        display.print("Q:Cancel");
      } else if (_otaPhase == OTA_PHASE_ERROR) {
        display.print("Q:Back");
      } else {
        display.print("Please wait...");
      }
    } else if (_editMode == EDIT_FILEMGR) {
      if (_fmPhase == FM_PHASE_CONFIRM) {
        display.print("Enter:Start  Q:Cancel");
      } else if (_fmPhase == FM_PHASE_WAITING) {
        display.print("Q:Stop");
      } else if (_fmPhase == FM_PHASE_ERROR) {
        display.print("Q:Back");
      } else {
        display.print("Please wait...");
      }
    #endif
    } else if (_editMode == EDIT_PICKER) {
#if defined(MECK_TWATCH)
      display.print("Tap:+/-   Hold:Save");
#else
      display.print("A/D:Choose Enter:Ok");
#endif
    } else if (_editMode == EDIT_NUMBER) {
#if defined(MECK_TWATCH)
      display.print("Tap:+/-   Hold:Save");
#else
      display.print("W/S:Adj Enter:Ok Q:Cancel");
#endif
    } else if (_editMode == EDIT_CONFIRM) {
      // Footer already covered by overlay
    } else {
      if (_subScreen == SUB_CHANNELS) {
        display.print("Q:Bk C:Share");
      } else if (_subScreen != SUB_NONE) {
#if defined(MECK_TWATCH)
        display.print("Hold:Edit");
#else
        display.print("Q:Back");
#endif
      } else {
#if defined(MECK_TWATCH)
        display.print("Hold:Edit");
#else
        display.print("Q:Bk");
#endif
      }
#if defined(MECK_TWATCH)
      const char* r = "Tap:Cycle";
#else
      const char* r = "Tap/Ent:Edit";
#endif
      display.setCursor(display.width() - display.getTextWidth(r) - 2, footerY);
      display.print(r);
    }

    #ifdef MECK_OTA_UPDATE
    // Poll web server frequently during OTA waiting/receiving or file manager phases
    if ((_editMode == EDIT_OTA &&
         (_otaPhase == OTA_PHASE_WAITING || _otaPhase == OTA_PHASE_RECEIVING)) ||
        (_editMode == EDIT_FILEMGR && _fmPhase == FM_PHASE_WAITING)) {
      return 200;  // 200ms — fast enough for web server responsiveness
    }
    #endif
#if defined(MECK_TWATCH)
    // Marquee running, or a wide row was just selected (its width was measured
    // this frame): refresh fast so scrolling starts promptly instead of after
    // the 1s idle interval.
    if (_tickerActive ||
        (_tickerRow == _cursor && _tickerTextW > display.width() - 10)) return 60;
#endif
    return _editMode != EDIT_NONE ? 700 : 1000;
  }

  // ---------------------------------------------------------------------------
  // Input handling
  // ---------------------------------------------------------------------------

  // Handle a keyboard character. Returns true if the screen consumed the input.
  bool handleKeyInput(char c) {
    // --- Confirmation dialog ---
    if (_editMode == EDIT_CONFIRM) {
      if (c == '\r' || c == 13) {
        if (_confirmAction == 1) {
          // Delete channel
          uint8_t chIdx = _rows[_cursor].param;
          deleteChannel(chIdx);
          rebuildRows();
        } else if (_confirmAction == 2) {
          applyRadioParams();
        } else if (_confirmAction == 3) {
          // Region nudge dismissed — user chose "Yes, leave unscoped"
          _editMode = EDIT_NONE;
          _confirmAction = 0;
          _onboarding = false;
          return false;  // Let caller navigate away from settings
        }
        _editMode = EDIT_NONE;
        _confirmAction = 0;
        return true;
      }
      if (c == KEY_CANCEL || c == 'q') {
        if (_confirmAction == 3) {
          // Region nudge cancelled — scroll to Default Region row
          _editMode = EDIT_NONE;
          _confirmAction = 0;
          // Find and scroll to ROW_DEFAULT_SCOPE
          for (int r = 0; r < _numRows; r++) {
            if (_rows[r].type == ROW_DEFAULT_SCOPE) { _cursor = r; break; }
          }
          ensureCursorVisible();
          return true;
        }
        _editMode = EDIT_NONE;
        _confirmAction = 0;
        return true;
      }
      return true;  // consume all keys in confirm mode
    }

    // --- Notification sound picker ---

    // --- Share contact picker ---
    if (_editMode == EDIT_SHARE_PICK) {
      if (c == 'w' || c == 'W' || c == 0xF2 || c == KEY_UP) {
        if (_sharePickerIdx > 0) _sharePickerIdx--;
        return true;
      }
      if (c == 's' || c == 'S' || c == 0xF1 || c == KEY_DOWN) {
        if (_sharePickerIdx < _shareContactCount - 1) _sharePickerIdx++;
        return true;
      }
      if (c == '\r' || c == 13) {
        if (_shareContactCount > 0) {
          _shareContactIdx = _shareContacts[_sharePickerIdx];
          _shareRequested = true;
          Serial.printf("Settings: share channel %d with contact %d\n",
                        _shareChannelIdx, _shareContactIdx);
        }
        _editMode = EDIT_NONE;
        return true;
      }
      if (c == KEY_CANCEL || c == 'q') {
        _editMode = EDIT_NONE;
        return true;
      }
      return true;  // consume all keys in picker mode
    }

    #ifdef MECK_OTA_UPDATE
    // --- OTA update flow ---
    if (_editMode == EDIT_OTA) {
      if (_otaPhase == OTA_PHASE_CONFIRM) {
        if (c == '\r' || c == 13) {
          _otaPhase = OTA_PHASE_AP_START;
          startOTAServer();
          return true;
        }
        if (c == KEY_CANCEL || c == 'q') {
          _editMode = EDIT_NONE;
          return true;
        }
      } else if (_otaPhase == OTA_PHASE_WAITING) {
        // Upload completed — main loop will detect and trigger flash
        if (_otaUploadOk) {
          return true;
        }
        if (c == KEY_CANCEL || c == 'q') {
          stopOTA();
          return true;
        }
      } else if (_otaPhase == OTA_PHASE_ERROR) {
        if (c == KEY_CANCEL || c == 'q') {
          stopOTA();
          return true;
        }
      }
      // Consume all keys during OTA
      return true;
    }

    // --- File Manager flow ---
    if (_editMode == EDIT_FILEMGR) {
      if (_fmPhase == FM_PHASE_CONFIRM) {
        if (c == '\r' || c == 13) {
          startFileMgrServer();
          return true;
        }
        if (c == KEY_CANCEL || c == 'q') {
          _editMode = EDIT_NONE;
          return true;
        }
      } else if (_fmPhase == FM_PHASE_WAITING) {
        if (c == KEY_CANCEL || c == 'q') {
          stopFileMgr();
          return true;
        }
      } else if (_fmPhase == FM_PHASE_ERROR) {
        if (c == KEY_CANCEL || c == 'q') {
          stopFileMgr();
          return true;
        }
      }
      // Consume all keys during file manager
      return true;
    }
    #endif

    #ifdef MECK_WIFI_COMPANION
    // --- WiFi setup flow ---
    if (_editMode == EDIT_WIFI) {
      if (_wifiPhase == WIFI_PHASE_SELECT) {
        if (c == 'w' || c == 'W') {
          if (_wifiSSIDSelected > 0) _wifiSSIDSelected--;
          return true;
        }
        if (c == 's' || c == 'S') {
          if (_wifiSSIDSelected < _wifiSSIDCount - 1) _wifiSSIDSelected++;
          return true;
        }
        if (c == 'r' || c == 'R') {
          // Rescan — lets user toggle hotspot on then retry
          performWifiScan();
          return true;
        }
#if defined(MECK_TWATCH)
        // Watch: the physical button (PWR=KEY_ENTER on S3, boot=KEY_NEXT on
        // Plus) rescans; a long-press (KEY_SELECT via mapTouchLongPress) confirms
        // the highlighted network. A tap only moves the highlight.
        if (c == KEY_ENTER || c == KEY_NEXT) {
          performWifiScan();
          return true;
        }
        if (c == KEY_SELECT) {
          if (_wifiSSIDCount == 0) {
            performWifiScan();
            return true;
          }
          // Confirm highlighted SSID — move to password entry
          _wifiPhase = WIFI_PHASE_PASSWORD;
          _wifiPassLen = 0;
          memset(_wifiPassBuf, 0, sizeof(_wifiPassBuf));
          _wifiFormLastChar = 0;
          _wantsWifiPwKb = true;   // UITask polls and opens the touch keyboard
          return true;
        }
#else
        if (c == '\r' || c == 13) {
          if (_wifiSSIDCount == 0) {
            // No networks — Enter rescans (same as R)
            performWifiScan();
            return true;
          }
          // Selected an SSID — move to password entry
          _wifiPhase = WIFI_PHASE_PASSWORD;
          _wifiPassLen = 0;
          memset(_wifiPassBuf, 0, sizeof(_wifiPassBuf));
          _wifiFormLastChar = 0;
          return true;
        }
#endif
        if (c == KEY_CANCEL || c == 'q') {
          _editMode = EDIT_NONE;
          _wifiPhase = WIFI_PHASE_IDLE;
          if (_onboarding) _onboarding = false;  // Skip WiFi, finish onboarding
          wifiReconnectSaved();  // Restore connection after scan disconnect
          return true;
        }
        return true;
      }

      if (_wifiPhase == WIFI_PHASE_PASSWORD) {
        if (c == '\r' || c == 13) {
          _wifiPassBuf[_wifiPassLen] = '\0';
          commitWifiConnect();
          return true;
        }
        if (c == '\b') {
          if (_wifiPassLen > 0) {
            _wifiPassLen--;
            _wifiPassBuf[_wifiPassLen] = '\0';
          }
          return true;
        }
        // Printable character
        if (c >= 32 && c < 127 && _wifiPassLen < 63) {
          _wifiPassBuf[_wifiPassLen++] = c;
          _wifiPassBuf[_wifiPassLen] = '\0';
          _wifiFormLastChar = millis();
          return true;
        }
        return true;
      }

      // Scanning and connecting phases consume all keys
      return true;
    }
    #endif

    // --- Text editing mode ---
    if (_editMode == EDIT_TEXT) {
      if (c == '\r' || c == 13) {
        // Confirm text edit
        SettingsRowType type = _rows[_cursor].type;
        if (type == ROW_NAME) {
          if (_editPos > 0) {
            strncpy(_prefs->node_name, _editBuf, sizeof(_prefs->node_name));
            _prefs->node_name[31] = '\0';
            the_mesh.savePrefs();
            Serial.printf("Settings: Name set to '%s'\n", _prefs->node_name);
          }
          _editMode = EDIT_NONE;
          if (_onboarding) {
            // Move to radio preset selection
            _cursor = 1;  // ROW_RADIO_PRESET
            ensureCursorVisible();
            startEditPicker(max(0, detectCurrentPreset()));
          }
        } else if (type == ROW_FREQ) {
          if (_editPos > 0) {
            float f = strtof(_editBuf, nullptr);
            float f_typed = f;
#if defined(MECK_TWATCH_1280)
            f = constrain(f, 2400.0f, 2500.0f);   // SX1280 tuning range
#else
            f = constrain(f, 400.0f, 2500.0f);
#endif
#if defined(MECK_TWATCH)
            if (f != f_typed) {
              char m[28];
              snprintf(m, sizeof(m), "Freq clamped to %.1f", f);
              queueClampAlert(m);
            }
#endif
            _prefs->freq = f;
            _radioChanged = true;
            Serial.printf("Settings: Freq typed to %.3f\n", f);
          }
          _editMode = EDIT_NONE;
        } else if (type == ROW_TX_POWER) {
          if (_editPos > 0) {
            int p = atoi(_editBuf);
            int p_typed = p;
            p = constrain(p, 1, MAX_LORA_TX_POWER);
#if defined(MECK_TWATCH)
            if (p != p_typed) {
              char m[28];
              snprintf(m, sizeof(m), "TX clamped to %d dBm", p);
              queueClampAlert(m);
            }
#endif
            _prefs->tx_power_dbm = (uint8_t)p;
            _radioChanged = true;
            Serial.printf("Settings: TX power typed to %d\n", p);
          }
          _editMode = EDIT_NONE;
        } else if (type == ROW_BW) {
          if (_editPos > 0) {
            // Snap the typed value to the nearest valid bandwidth -- the same
            // discrete set the W/S cycle uses.
#if defined(MECK_TWATCH_1280)
            // SX1280 (2.4 GHz) LoRa bandwidths (RadioLib accepts 203.125-1625).
            static const float BW_STEPS[] = { 203.125f, 406.25f, 812.5f, 1625.0f };
#else
            static const float BW_STEPS[] = { 31.25f, 62.5f, 125.0f, 250.0f, 500.0f };
#endif
            float typed = atof(_editBuf);
            float best = BW_STEPS[0];
            for (unsigned i = 1; i < sizeof(BW_STEPS) / sizeof(BW_STEPS[0]); i++) {
              if (fabsf(typed - BW_STEPS[i]) < fabsf(typed - best)) best = BW_STEPS[i];
            }
            _prefs->bw = best;
#if defined(MECK_TWATCH)
            if (fabsf(typed - best) > 0.01f) {
              char m[28];
              snprintf(m, sizeof(m), "BW snapped to %.3f", best);
              queueClampAlert(m);
            }
#endif
            _radioChanged = true;
            Serial.printf("Settings: BW typed, snapped to %.2f\n", best);
          }
          _editMode = EDIT_NONE;
        } else if (type == ROW_ADD_CHANNEL) {
          if (_editPos > 0) {
            createChannel(_editBuf);
            rebuildRows();
          }
          _editMode = EDIT_NONE;
        } else if (type == ROW_LATITUDE || type == ROW_LONGITUDE) {
          // Empty clears to 0. Out-of-range values are rejected rather than
          // clamped, so a typo cannot silently advertise the wrong place.
          if (_editBuf[0] == '\0') {
            setPosition(type == ROW_LATITUDE ? 0.0 : positionLat(),
                        type == ROW_LATITUDE ? positionLon() : 0.0);
            the_mesh.savePrefs();
          } else {
            double v = atof(_editBuf);
            double lim = (type == ROW_LATITUDE) ? 90.0 : 180.0;
            if (v >= -lim && v <= lim) {
              if (type == ROW_LATITUDE) setPosition(v, positionLon());
              else                      setPosition(positionLat(), v);
              the_mesh.savePrefs();
              Serial.printf("Settings: %s = %.6f\n",
                            type == ROW_LATITUDE ? "Latitude" : "Longitude", v);
            } else {
              Serial.printf("Settings: %s out of range (%.6f)\n",
                            type == ROW_LATITUDE ? "Latitude" : "Longitude", v);
            }
          }
          _editMode = EDIT_NONE;
        } else if (type == ROW_DEFAULT_SCOPE) {
          // Save device-wide default scope
          strncpy(_prefs->default_scope_name, _editBuf, sizeof(_prefs->default_scope_name));
          _prefs->default_scope_name[30] = '\0';
          if (_editBuf[0]) {
            TransportKey key;
            the_mesh.deriveScopeKey(_editBuf, key);
            memcpy(_prefs->default_scope_key, key.key, sizeof(_prefs->default_scope_key));
          } else {
            memset(_prefs->default_scope_key, 0, sizeof(_prefs->default_scope_key));
          }
          the_mesh.savePrefs();
          Serial.printf("Settings: Default scope set to '%s'\n",
                        _editBuf[0] ? _editBuf : "(unscoped)");
          _editMode = EDIT_NONE;
        } else if (type == ROW_CHANNEL) {
          // Save per-channel scope
          uint8_t chIdx = _rows[_cursor].param;
          ChannelDetails ch;
          if (the_mesh.getChannel(chIdx, ch)) {
            strncpy(ch.scope_name, _editBuf, sizeof(ch.scope_name));
            ch.scope_name[30] = '\0';
            // Derive the transport key at set time and store the bytes in the
            // channel record -- same architecture as the device default. An
            // empty entry clears both (channel reverts to device default).
            if (ch.scope_name[0]) {
              TransportKey key;
              the_mesh.deriveScopeKey(ch.scope_name, key);
              memcpy(ch.scope_key, key.key, sizeof(ch.scope_key));
            } else {
              memset(ch.scope_key, 0, sizeof(ch.scope_key));
            }
            the_mesh.setChannel(chIdx, ch);
            the_mesh.saveChannels();
            Serial.printf("Settings: Channel %d scope set to '%s'\n",
                          chIdx, _editBuf[0] ? _editBuf : "(device default)");
          }
          _editMode = EDIT_NONE;
        }
        return true;
      }
      if (c == KEY_CANCEL) {
        _editMode = EDIT_NONE;
        return true;
      }
      if (c == '\b') {
        if (_editPos > 0) {
          _editPos--;
          _editBuf[_editPos] = '\0';
        }
        return true;
      }
      // Printable character
      if (c >= 32 && c < 127 && _editPos < SETTINGS_TEXT_BUF - 1) {
        _editBuf[_editPos++] = c;
        _editBuf[_editPos] = '\0';
        return true;
      }
      return true;  // consume all keys in text edit
    }

    // --- Picker mode (radio preset or contact mode) ---
    if (_editMode == EDIT_PICKER) {
      SettingsRowType type = _rows[_cursor].type;

      if (c == 'a' || c == 'A') {
        if (type == ROW_CONTACT_MODE) {
          _editPickerIdx--;
          if (_editPickerIdx < 0) _editPickerIdx = CONTACT_MODE_COUNT - 1;
        } else if (type == ROW_GPS_BAUD) {
          _editPickerIdx--;
          if (_editPickerIdx < 0) _editPickerIdx = GPS_BAUD_OPTION_COUNT - 1;
        } else if (type == ROW_FONT_STYLE) {
          _editPickerIdx--;
          if (_editPickerIdx < 0) _editPickerIdx = MECK_FONT_STYLE_COUNT - 1;
          _prefs->ui_font_style = _editPickerIdx;  // Live preview
        } else {
          // Radio preset
          _editPickerIdx--;
          if (_editPickerIdx < 0) _editPickerIdx = (int)NUM_RADIO_PRESETS - 1;
        }
        return true;
      }
      if (c == 'd' || c == 'D') {
        if (type == ROW_CONTACT_MODE) {
          _editPickerIdx++;
          if (_editPickerIdx >= CONTACT_MODE_COUNT) _editPickerIdx = 0;
        } else if (type == ROW_GPS_BAUD) {
          _editPickerIdx++;
          if (_editPickerIdx >= GPS_BAUD_OPTION_COUNT) _editPickerIdx = 0;
        } else if (type == ROW_FONT_STYLE) {
          _editPickerIdx++;
          if (_editPickerIdx >= MECK_FONT_STYLE_COUNT) _editPickerIdx = 0;
          _prefs->ui_font_style = _editPickerIdx;  // Live preview
        } else {
          // Radio preset
          _editPickerIdx++;
          if (_editPickerIdx >= (int)NUM_RADIO_PRESETS) _editPickerIdx = 0;
        }
        return true;
      }
      if (c == '\r' || c == 13) {
        if (type == ROW_CONTACT_MODE) {
          applyContactMode(_editPickerIdx);
          _editMode = EDIT_NONE;
        } else if (type == ROW_GPS_BAUD) {
          _prefs->gps_baudrate = GPS_BAUD_OPTIONS[_editPickerIdx];
          the_mesh.savePrefs();
          _editMode = EDIT_NONE;
          Serial.printf("Settings: GPS baud set to %lu (reboot to apply)\n",
                        (unsigned long)_prefs->gps_baudrate);
        } else if (type == ROW_FONT_STYLE) {
          _prefs->ui_font_style = _editPickerIdx;
          the_mesh.savePrefs();
          _editMode = EDIT_NONE;
          Serial.printf("Settings: Font style = %s (%d)\n",
                        meckFontStyleName(_prefs->ui_font_style),
                        _prefs->ui_font_style);
        } else {
          // Apply radio preset
          if (_editPickerIdx >= 0 && _editPickerIdx < (int)NUM_RADIO_PRESETS) {
            const RadioPreset& p = RADIO_PRESETS[_editPickerIdx];
            _prefs->freq = p.freq;
            _prefs->bw = p.bw;
            _prefs->sf = p.sf;
            _prefs->cr = p.cr;
            _prefs->tx_power_dbm = p.tx_power;
            _radioChanged = true;
          }
          _editMode = EDIT_NONE;
          if (_onboarding) {
            applyRadioParams();
          #ifdef MECK_WIFI_COMPANION
            // Move to WiFi setup before finishing onboarding
            for (int r = 0; r < _numRows; r++) {
              if (_rows[r].type == ROW_WIFI_SETUP) {
                _cursor = r;
                break;
              }
            }
            ensureCursorVisible();
            // Auto-launch the WiFi scan
            _editMode = EDIT_WIFI;
            performWifiScan();
          #else
            _onboarding = false;
          #endif
          }
        }
        return true;
      }
      if (c == KEY_CANCEL || c == 'q') {
        // Revert live preview if font style picker was active
        if (type == ROW_FONT_STYLE) {
          _prefs->ui_font_style = _fontPickerOriginal;
        }
        _editMode = EDIT_NONE;
        return true;
      }
      return true;
    }

    // --- Number editing mode ---
    if (_editMode == EDIT_NUMBER) {
      SettingsRowType type = _rows[_cursor].type;

      if (c == 'w' || c == 'W') {
        switch (type) {
          case ROW_BW:
            // Cycle through common bandwidths
#if defined(MECK_TWATCH_1280)
            if (_editFloat < 203.125f) _editFloat = 203.125f;
            else if (_editFloat < 406.25f) _editFloat = 406.25f;
            else if (_editFloat < 812.5f) _editFloat = 812.5f;
            else _editFloat = 1625.0f;
#else
            if (_editFloat < 31.25f) _editFloat = 31.25f;
            else if (_editFloat < 62.5f) _editFloat = 62.5f;
            else if (_editFloat < 125.0f) _editFloat = 125.0f;
            else if (_editFloat < 250.0f) _editFloat = 250.0f;
            else _editFloat = 500.0f;
#endif
            break;
          case ROW_SF:      if (_editInt < 12) _editInt++; break;
          case ROW_CR:      if (_editInt < 8)  _editInt++; break;
          case ROW_TX_POWER: if (_editInt < MAX_LORA_TX_POWER) _editInt++; break;
          case ROW_UTC_OFFSET: if (_editInt < 14) _editInt++; break;
          case ROW_BACKLIGHT_BRIGHTNESS: if (_editInt < 100) { _editInt += 5; if (_editInt > 100) _editInt = 100; } break;
          case ROW_KB_BACKLIGHT: if (_editInt < 100) { _editInt += 5; if (_editInt > 100) _editInt = 100; } break;
          case ROW_PATH_HASH_SIZE: if (_editInt < 3) _editInt++; break;
          default: break;
        }
        return true;
      }
      if (c == 's' || c == 'S') {
        switch (type) {
          case ROW_BW:
#if defined(MECK_TWATCH_1280)
            if (_editFloat > 812.5f) _editFloat = 812.5f;
            else if (_editFloat > 406.25f) _editFloat = 406.25f;
            else if (_editFloat > 203.125f) _editFloat = 203.125f;
            else _editFloat = 203.125f;
#else
            if (_editFloat > 250.0f) _editFloat = 250.0f;
            else if (_editFloat > 125.0f) _editFloat = 125.0f;
            else if (_editFloat > 62.5f) _editFloat = 62.5f;
            else _editFloat = 31.25f;
#endif
            break;
          case ROW_SF:      if (_editInt > 5)  _editInt--; break;
          case ROW_CR:      if (_editInt > 5)  _editInt--; break;
          case ROW_TX_POWER: if (_editInt > 1)  _editInt--; break;
          case ROW_UTC_OFFSET: if (_editInt > -12) _editInt--; break;
          case ROW_BACKLIGHT_BRIGHTNESS: if (_editInt > 5) { _editInt -= 5; if (_editInt < 5) _editInt = 5; } break;
          case ROW_KB_BACKLIGHT: if (_editInt > 5) { _editInt -= 5; if (_editInt < 5) _editInt = 5; } break;
          case ROW_PATH_HASH_SIZE: if (_editInt > 1) _editInt--; break;
          default: break;
        }
        return true;
      }
      if (c == '\r' || c == 13) {
        // Confirm number edit
        switch (type) {
          case ROW_BW:
            _prefs->bw = _editFloat;
            _radioChanged = true;
            break;
          case ROW_SF:
            _prefs->sf = (uint8_t)constrain(_editInt, 5, 12);
            _radioChanged = true;
            break;
          case ROW_CR:
            _prefs->cr = (uint8_t)constrain(_editInt, 5, 8);
            _radioChanged = true;
            break;
          case ROW_TX_POWER:
            _prefs->tx_power_dbm = (uint8_t)constrain(_editInt, 1, MAX_LORA_TX_POWER);
            _radioChanged = true;
            break;
          case ROW_UTC_OFFSET:
            _prefs->utc_offset_hours = (int8_t)constrain(_editInt, -12, 14);
            the_mesh.savePrefs();
            break;
          case ROW_BACKLIGHT_BRIGHTNESS:
            _prefs->backlight_brightness_pct = (uint8_t)constrain(_editInt, 5, 100);
            the_mesh.savePrefs();
            break;
          case ROW_KB_BACKLIGHT:
            _prefs->kb_backlight_pct = (uint8_t)constrain(_editInt, 5, 100);
            the_mesh.savePrefs();
            break;
          case ROW_PATH_HASH_SIZE:
            _prefs->path_hash_mode = (uint8_t)constrain(_editInt - 1, 0, 2);  // display 1-3, store 0-2
            the_mesh.savePrefs();
            break;
          default: break;
        }
        _editMode = EDIT_NONE;
        return true;
      }
      if (c == KEY_CANCEL || c == 'q') {
        _editMode = EDIT_NONE;
        return true;
      }
      return true;
    }

    // --- Normal browsing mode ---

    // W/S: navigate, Shift+W/S: page scroll
    if (c == 'W') {
      // Shift+W: page up
      int pageSize = (128 - 14 - 14) / _prefs->smallLineH();
      if (pageSize < 3) pageSize = 3;
      _cursor = max(0, _cursor - pageSize);
      skipNonSelectable(-1);
      Serial.printf("Settings: page up cursor=%d/%d\n", _cursor, _numRows);
      return true;
    }
    if (c == 'w') {
      if (_cursor > 0) {
        _cursor--;
        skipNonSelectable(-1);
      }
      Serial.printf("Settings: cursor=%d/%d row=%d\n", _cursor, _numRows, _rows[_cursor].type);
      return true;
    }
    if (c == 'S') {
      // Shift+S: page down
      int pageSize = (128 - 14 - 14) / _prefs->smallLineH();
      if (pageSize < 3) pageSize = 3;
      _cursor = min(_numRows - 1, _cursor + pageSize);
      skipNonSelectable(1);
      Serial.printf("Settings: page down cursor=%d/%d\n", _cursor, _numRows);
      return true;
    }
    if (c == 's') {
      if (_cursor < _numRows - 1) {
        _cursor++;
        skipNonSelectable(1);
      }
      Serial.printf("Settings: cursor=%d/%d row=%d\n", _cursor, _numRows, _rows[_cursor].type);
      return true;
    }

    // Enter: start editing the selected row
    if (c == '\r' || c == 13) {
      SettingsRowType type = _rows[_cursor].type;
      switch (type) {
        case ROW_NAME:
#if defined(MECK_TWATCH)
          strncpy(_kbInitText, _prefs->node_name, sizeof(_kbInitText) - 1);
          _kbInitText[sizeof(_kbInitText) - 1] = '\0';
          _wantsKb = true;   // UITask::loop() polls and opens the touch keyboard
#else
          startEditText(_prefs->node_name);
#endif
          break;
        case ROW_RADIO_PRESET:
          startEditPicker(max(0, detectCurrentPreset()));
          break;
        case ROW_FREQ: {
          // Use text input so user can type exact frequencies like 916.575
#if defined(MECK_TWATCH)
          snprintf(_kbInitText, sizeof(_kbInitText), "%.3f", _prefs->freq);
          _wantsKb = true;   // UITask::loop() polls and opens the touch keyboard
#else
          char freqStr[16];
          snprintf(freqStr, sizeof(freqStr), "%.3f", _prefs->freq);
          startEditText(freqStr);
#endif
          break;
        }
        case ROW_BW:
#if defined(MECK_TWATCH)
          // Watch: type the value; it snaps to the nearest valid bandwidth.
          snprintf(_kbInitText, sizeof(_kbInitText), "%.1f", _prefs->bw);
          _wantsKb = true;   // UITask::loop() polls and opens the touch keyboard
#else
          startEditFloat(_prefs->bw);
#endif
          break;
        case ROW_SF:
          startEditInt(_prefs->sf);
          break;
        case ROW_CR:
          startEditInt(_prefs->cr);
          break;
        case ROW_TX_POWER:
#if defined(MECK_TWATCH)
          // Watch: type the value (range is wide; cycling is tedious).
          snprintf(_kbInitText, sizeof(_kbInitText), "%d", _prefs->tx_power_dbm);
          _wantsKb = true;   // UITask::loop() polls and opens the touch keyboard
#else
          startEditInt(_prefs->tx_power_dbm);
#endif
          break;
        case ROW_UTC_OFFSET:
          startEditInt(_prefs->utc_offset_hours);
          break;
        case ROW_BACKLIGHT_BRIGHTNESS:
          startEditInt(_prefs->backlight_brightness_pct);
          break;
        case ROW_KB_BACKLIGHT:
          startEditInt(_prefs->kb_backlight_pct);
          break;
        case ROW_MSG_NOTIFY:
          _prefs->kb_flash_notify = _prefs->kb_flash_notify ? 0 : 1;
          the_mesh.savePrefs();
          Serial.printf("Settings: Msg flash notify = %s\n",
                        _prefs->kb_flash_notify ? "ON" : "OFF");
          break;
        case ROW_PATH_HASH_SIZE:
          startEditInt(_prefs->path_hash_mode + 1);  // display as 1-3
          break;
        case ROW_GPS_BAUD:
          startEditPicker(findGpsBaudIndex(_prefs->gps_baudrate));
          break;
        case ROW_DARK_MODE:
          _prefs->dark_mode = _prefs->dark_mode ? 0 : 1;
          the_mesh.savePrefs();
          Serial.printf("Settings: Dark mode = %s\n",
                        _prefs->dark_mode ? "ON" : "OFF");
          break;
        case ROW_LARGE_FONT:
          _prefs->large_font = _prefs->large_font ? 0 : 1;
          the_mesh.savePrefs();
          Serial.printf("Settings: Font size = %s\n",
                        _prefs->large_font ? "LARGER" : "TINY");
          break;
        case ROW_FONT_STYLE:
          _fontPickerOriginal = _prefs->ui_font_style;
          startEditPicker(_prefs->ui_font_style);
          break;
        #ifdef MECK_WIFI_COMPANION
        case ROW_WIFI_SETUP: {
          // Launch WiFi scan → select → password → connect flow
          _editMode = EDIT_WIFI;
          performWifiScan();
          break;
        }
        case ROW_WIFI_TOGGLE:
          if (WiFi.getMode() != WIFI_OFF) {
            // Turn WiFi OFF
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            Serial.println("Settings: WiFi radio OFF");
          } else {
            // Turn WiFi ON — reconnect using saved credentials
            WiFi.mode(WIFI_STA);
            if (SD.exists("/web/wifi.cfg")) {
              File f = SD.open("/web/wifi.cfg", FILE_READ);
              if (f) {
                String ssid = f.readStringUntil('\n'); ssid.trim();
                String pass = f.readStringUntil('\n'); pass.trim();
                f.close();
                digitalWrite(SDCARD_CS, HIGH);
                if (ssid.length() > 0) {
                  WiFi.begin(ssid.c_str(), pass.c_str());
                  unsigned long timeout = millis() + 8000;
                  while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
                    delay(100);
                  }
                  if (WiFi.status() == WL_CONNECTED) {
                    Serial.printf("Settings: WiFi ON, connected to %s\n", ssid.c_str());
                  } else {
                    Serial.println("Settings: WiFi ON, but connection failed");
                  }
                }
              } else {
                digitalWrite(SDCARD_CS, HIGH);
              }
            }
            Serial.println("Settings: WiFi radio ON");
          }
          break;
        #endif

        // --- Contact mode picker ---
        case ROW_CONTACT_MODE:
          startEditPicker(getContactMode());
          break;

        // --- Contact sub-toggles (flip bit and save) ---
        case ROW_AUTOADD_CHAT:
          _prefs->autoadd_config ^= AUTO_ADD_CHAT;
          the_mesh.savePrefs();
          Serial.printf("Settings: Auto-add Chat = %s\n",
                        (_prefs->autoadd_config & AUTO_ADD_CHAT) ? "ON" : "OFF");
          break;
        case ROW_AUTOADD_REPEATER:
          _prefs->autoadd_config ^= AUTO_ADD_REPEATER;
          the_mesh.savePrefs();
          Serial.printf("Settings: Auto-add Repeater = %s\n",
                        (_prefs->autoadd_config & AUTO_ADD_REPEATER) ? "ON" : "OFF");
          break;
        case ROW_AUTOADD_ROOM:
          _prefs->autoadd_config ^= AUTO_ADD_ROOM_SERVER;
          the_mesh.savePrefs();
          Serial.printf("Settings: Auto-add Room = %s\n",
                        (_prefs->autoadd_config & AUTO_ADD_ROOM_SERVER) ? "ON" : "OFF");
          break;
        case ROW_AUTOADD_SENSOR:
          _prefs->autoadd_config ^= AUTO_ADD_SENSOR;
          the_mesh.savePrefs();
          Serial.printf("Settings: Auto-add Sensor = %s\n",
                        (_prefs->autoadd_config & AUTO_ADD_SENSOR) ? "ON" : "OFF");
          break;
        case ROW_AUTOADD_OVERWRITE:
          _prefs->autoadd_config ^= AUTO_ADD_OVERWRITE_OLDEST;
          the_mesh.savePrefs();
          Serial.printf("Settings: Overwrite oldest = %s\n",
                        (_prefs->autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) ? "ON" : "OFF");
          break;

        // --- Submenu folder rows ---
        case ROW_CONTACTS_SUBMENU:
          _savedTopCursor = _cursor;
          _subScreen = SUB_CONTACTS;
          _cursor = 0;
          _scrollTop = 0;
          rebuildRows();
          Serial.println("Settings: entered Contacts sub-screen");
          break;
        case ROW_CHANNELS_SUBMENU:
#if defined(MECK_TWATCH)
          // Watch: the desktop sub-screen's per-channel actions are keyboard
          // hotkeys; hand off to the standalone WatchChannelConfigScreen.
          _wantsWatchChannels = true;   // UITask::loop() polls and opens it
#else
          _savedTopCursor = _cursor;
          _subScreen = SUB_CHANNELS;
          _cursor = 0;
          _scrollTop = 0;
          rebuildRows();
          Serial.println("Settings: entered Channels sub-screen");
#endif
          break;

        case ROW_RXLOG:
          _rxlogRequested = true;
          break;

#if defined(MECK_TWATCH)
        case ROW_FILE_MANAGER:
          // Toggle the WiFi AP file explorer. While active the row shows the
          // AP name; browse to http://192.168.4.1/ once connected.
          if (wfmActive()) wfmStop(); else wfmStart();
          break;
#endif

#if defined(MECK_TWATCH)
        case ROW_EXPERIMENTAL_SUBMENU:
          _savedTopCursor = _cursor;
          _subScreen = SUB_EXPERIMENTAL;
          _cursor = 0;
          _scrollTop = 0;
          rebuildRows();
          Serial.println("Settings: entered Experimental Features sub-screen");
          break;

        case ROW_CANNED_SUBMENU:
          _savedTopCursor = _cursor;
          _subScreen = SUB_CANNED;
          _cursor = 0;
          _scrollTop = 0;
          rebuildRows();
          Serial.println("Settings: entered Canned Messages sub-screen");
          break;

        case ROW_CANNED_SLOT: {
          uint8_t slot = _rows[_cursor].param;
          strncpy(_kbInitText, _prefs->canned_msgs[slot], sizeof(_kbInitText) - 1);
          _kbInitText[sizeof(_kbInitText) - 1] = '\0';
          _wantsKb = true;   // UITask::loop() polls and opens the touch keyboard
          break;
        }

        case ROW_RX_DUTY_CYCLE:
          _prefs->rx_duty_cycle = _prefs->rx_duty_cycle ? 0 : 1;
          the_mesh.savePrefs();
          _wantsApplyRxDuty = true;   // UITask::loop() polls and applies to radio_driver
          Serial.printf("Settings: Rx Duty Cycle = %s\n",
                        _prefs->rx_duty_cycle ? "ON" : "OFF");
          break;

        case ROW_POSITION_MODE:
          _prefs->position_mode = (_prefs->position_mode >= 2) ? 0 : _prefs->position_mode + 1;
          applyPositionMode();
          the_mesh.savePrefs();
          rebuildRows();   // Lat/Lon rows appear or disappear with the mode
          Serial.printf("Settings: Position mode = %d\n", _prefs->position_mode);
          break;
        case ROW_LATITUDE:
        case ROW_LONGITUDE: {
          if (_prefs->position_mode == 2) break;   // GPS owns the value in Auto
          char cur[24];
          snprintf(cur, sizeof(cur), "%.6f",
                   _rows[_cursor].type == ROW_LATITUDE ? positionLat() : positionLon());
#if defined(MECK_TWATCH)
          strncpy(_kbInitText, cur, sizeof(_kbInitText) - 1);
          _kbInitText[sizeof(_kbInitText) - 1] = '\0';
          _wantsKb = true;
#else
          startEditText(cur);
#endif
          break;
        }
        case ROW_RX_DUTY_LEVEL:
          _prefs->rx_duty_level = (_prefs->rx_duty_level >= 2) ? 0 : _prefs->rx_duty_level + 1;
          the_mesh.savePrefs();
          _wantsApplyRxLevel = true;   // UITask::loop() polls and applies to radio_driver
          Serial.printf("Settings: Nap Depth = %d\n", _prefs->rx_duty_level);
          break;
#endif

        case ROW_ADD_CHANNEL:
#if defined(MECK_TWATCH)
          _kbInitText[0] = '\0';
          _wantsKb = true;   // UITask::loop() polls and opens the touch keyboard
#else
          startEditText("");
#endif
          break;

        #ifdef MECK_OTA_UPDATE
        case ROW_OTA_TOOLS_SUBMENU:
          _savedTopCursor = _cursor;
          _subScreen = SUB_OTA_TOOLS;
          _cursor = 0;
          _scrollTop = 0;
          rebuildRows();
          Serial.println("Settings: entered OTA Tools sub-screen");
          break;
        case ROW_FW_UPDATE:
          startOTA();
          break;
        case ROW_SD_FILE_MGR:
          startFileMgr();
          break;
        #endif

        #ifdef MECK_HAS_CONFIG_EXPORT
        case ROW_EXPORT_IMPORT_SUBMENU:
          _savedTopCursor = _cursor;
          _subScreen = SUB_EXPORT_IMPORT;
          _cursor = 0;
          _scrollTop = 0;
          rebuildRows();
          Serial.println("Settings: entered Export/Import sub-screen");
          break;

        case ROW_EXPORT_TO_SD:
          _savedExportCursor = _cursor;
          _subScreen = SUB_EXPORT_FLAGS;
          _cursor = 0;
          _scrollTop = 0;
          rebuildRows();
          Serial.println("Settings: entered Export flags sub-screen");
          break;

        case ROW_IMPORT_FROM_SD:
          _importRequested = true;
          Serial.println("Settings: import requested");
          break;

        case ROW_EXPORT_IDENTITY:
          _exportFlags ^= MECK_EXPORT_IDENTITY;
          break;
        case ROW_EXPORT_RADIO:
          _exportFlags ^= MECK_EXPORT_RADIO;
          break;
        case ROW_EXPORT_CHANNELS:
          _exportFlags ^= MECK_EXPORT_CHANNELS;
          break;
        case ROW_EXPORT_CONTACTS:
          _exportFlags ^= MECK_EXPORT_CONTACTS;
          break;
        case ROW_EXPORT_AUTOADD:
          _exportFlags ^= MECK_EXPORT_AUTOADD;
          break;

        case ROW_EXPORT_NOW:
          if (_exportFlags == 0) {
            Serial.println("Settings: export requested but no sections selected");
          } else {
            _exportRequested = true;
            Serial.printf("Settings: export requested (flags=0x%02X)\n", _exportFlags);
          }
          break;
        #endif
        case ROW_CHANNEL: {
          // Enter on a channel row → edit its region scope
          uint8_t chIdx = _rows[_cursor].param;
          ChannelDetails ch;
          if (the_mesh.getChannel(chIdx, ch)) {
            startEditText(ch.scope_name);
          }
          break;
        }
        case ROW_DEFAULT_SCOPE:
#if defined(MECK_TWATCH)
          strncpy(_kbInitText, _prefs->default_scope_name, sizeof(_kbInitText) - 1);
          _kbInitText[sizeof(_kbInitText) - 1] = '\0';
          _wantsKb = true;   // UITask::loop() polls and opens the touch keyboard
#else
          startEditText(_prefs->default_scope_name);
#endif
          break;
        case ROW_CLIENT_REPEAT:
          // Toggle only on a whitelisted frequency (same rule the app is held
          // to); on other frequencies the row reads "n/a" and does nothing.
          if (the_mesh.isValidClientRepeatFreq((uint32_t)(_prefs->freq * 1000.0f + 0.5f))) {
            _prefs->setRepeatEn(!_prefs->isRepeatEn());
            the_mesh.savePrefs();
          }
          break;

        case ROW_PUB_KEY:
        case ROW_FIRMWARE:
#if defined(MECK_TWATCH)
        case ROW_MCU_TEMP:
#endif
          // Not directly editable on Enter
          break;
        default:
          break;
      }
      return true;
    }

    // X: delete channel (when on a channel row, idx > 0)
    if (c == 'x' || c == 'X') {
      if (_rows[_cursor].type == ROW_CHANNEL && _rows[_cursor].param > 0) {
        _editMode = EDIT_CONFIRM;
        _confirmAction = 1;
        return true;
      }
    }

    // N: cycle notification preference (All -> Mentions -> None -> All)
    if (c == 'n' || c == 'N') {
      if (_rows[_cursor].type == ROW_CHANNEL) {
        uint8_t chIdx = _rows[_cursor].param;
        uint8_t cur = _prefs->channel_notif[chIdx];
        _prefs->channel_notif[chIdx] = (cur + 1) % 3;
        the_mesh.savePrefs();
        const char* labels[] = {"All", "Mentions", "Off"};
        Serial.printf("Settings: Channel %d notif -> %s\n",
                      chIdx, labels[_prefs->channel_notif[chIdx]]);
        return true;
      }
    }

    // C: share channel with a contact via DM (channels sub-screen only)
    if ((c == 'c' || c == 'C') && _subScreen == SUB_CHANNELS) {
      if (_rows[_cursor].type == ROW_CHANNEL) {
        _shareChannelIdx = _rows[_cursor].param;
        // Populate contact list with DM-capable contacts, favourites first
        _shareContactCount = 0;
        int numContacts = the_mesh.getNumContacts();
        // First pass: favourites
        for (int ci = 0; ci < numContacts && _shareContactCount < SHARE_MAX_CONTACTS; ci++) {
          ContactInfo contact;
          if (the_mesh.getContactByIdx(ci, contact) && contact.type == ADV_TYPE_CHAT
              && (contact.flags & 0x01)) {
            _shareContacts[_shareContactCount++] = ci;
          }
        }
        int favCount = _shareContactCount;
        // Second pass: non-favourites
        for (int ci = 0; ci < numContacts && _shareContactCount < SHARE_MAX_CONTACTS; ci++) {
          ContactInfo contact;
          if (the_mesh.getContactByIdx(ci, contact) && contact.type == ADV_TYPE_CHAT
              && !(contact.flags & 0x01)) {
            _shareContacts[_shareContactCount++] = ci;
          }
        }
        // Sort each group alphabetically by name
        auto sortRange = [&](int start, int end) {
          for (int a = start; a < end - 1; a++) {
            for (int b = a + 1; b < end; b++) {
              ContactInfo ca, cb;
              the_mesh.getContactByIdx(_shareContacts[a], ca);
              the_mesh.getContactByIdx(_shareContacts[b], cb);
              if (strcasecmp(ca.name, cb.name) > 0) {
                int tmp = _shareContacts[a];
                _shareContacts[a] = _shareContacts[b];
                _shareContacts[b] = tmp;
              }
            }
          }
        };
        sortRange(0, favCount);
        sortRange(favCount, _shareContactCount);
        _sharePickerIdx = 0;
        _sharePickerScroll = 0;
        _editMode = EDIT_SHARE_PICK;
        Serial.printf("Settings: sharing channel %d, %d contacts available\n",
                      _shareChannelIdx, _shareContactCount);
        return true;
      }
    }

    // T: open notification tone picker

    // Shift+Del: back -- if in sub-screen, return to top level; else exit settings
    if (c == KEY_CANCEL || c == 'q') {
      #ifdef MECK_HAS_CONFIG_EXPORT
      if (_subScreen == SUB_EXPORT_FLAGS) {
        // Return to Export/Import sub-screen
        _subScreen = SUB_EXPORT_IMPORT;
        rebuildRows();
        _cursor = _savedExportCursor;
        if (_cursor >= _numRows) _cursor = _numRows - 1;
        skipNonSelectable(1);
        Serial.println("Settings: back to Export/Import");
        return true;
      }
      #endif
      if (_subScreen != SUB_NONE) {
        // Return to top-level settings list
        _subScreen = SUB_NONE;
        rebuildRows();
        _cursor = _savedTopCursor;
        if (_cursor >= _numRows) _cursor = _numRows - 1;
        skipNonSelectable(1);
        Serial.println("Settings: back to top level");
        return true;
      }
      if (_radioChanged) {
        _editMode = EDIT_CONFIRM;
        _confirmAction = 2;
        return true;
      }
      // Nudge if no region is set anywhere (device default empty AND no per-channel scopes)
      if (_prefs->default_scope_name[0] == '\0') {
        bool anyChannelScoped = false;
        for (uint8_t ci = 0; ci < MAX_GROUP_CHANNELS && !anyChannelScoped; ci++) {
          ChannelDetails ch;
          if (the_mesh.getChannel(ci, ch) && ch.name[0] != '\0' && ch.scope_name[0] != '\0') {
            anyChannelScoped = true;
          }
        }
        if (!anyChannelScoped) {
          _editMode = EDIT_CONFIRM;
          _confirmAction = 3;
          return true;
        }
      }
      _onboarding = false;
      return false;  // Let the caller handle navigation back
    }

    return true;  // Consume all other keys (don't let caller exit)
  }

  // Override handleInput for UIScreen compatibility (used by injectKey)
  bool handleInput(char c) override {
    return handleKeyInput(c);
  }
};