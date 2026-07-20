#include <Arduino.h>   // needed for PlatformIO
#if defined(ESP32) && defined(BLE_PIN_CODE)
  #include <esp_bt.h>    // for esp_bt_controller_mem_release (web reader WiFi)
#endif
#if defined(ESP32) && defined(MECK_OTA_UPDATE)
  #include <esp_ota_ops.h>
#endif
#ifdef ESP32
  #include <esp_partition.h>
#endif
#include <Mesh.h>
#include "MyMesh.h"
#include "variant.h"   // Board-specific defines (HAS_GPS, etc.)
#include "target.h"    // For sensors, board, etc.
#include "CPUPowerManager.h"

// T-Deck Pro Keyboard support

// =============================================================================
// Touch Input — unified across T5S3 (GT911) and T-Deck Pro (CST328)
// =============================================================================

// Define MECK_TOUCH_ENABLED for any platform with touch support
#if defined(MECK_TWATCH)
  #define MECK_TOUCH_ENABLED 1
#endif

// --- T-Watch S3 Plus: screen headers for the touch UI ---
// The watch needs the same concrete screen types as the gesture machine casts
// to, but none of the T5S3/T-Deck hardware baggage (GT911, SD, TCA8418 keyboard).
#if defined(MECK_TWATCH)
  #include "TextReaderScreen.h"
  #include "NotesScreen.h"
  #include "ContactsScreen.h"
  #include "ChannelScreen.h"
  #include "ChannelPickerScreen.h"
  #include "MeckExport.h"
  #include "MeckImport.h"
  #include "SettingsScreen.h"
  #include "RepeaterAdminScreen.h"
  #include "DiscoveryScreen.h"
  #include "LastHeardScreen.h"
  #include "PathEditorScreen.h"
  #include "Tracescreen.h"
  #include "GamesMenuScreen.h"
  #include "SnakeScreen.h"
  #include "MinesweeperScreen.h"
#endif

// --- T5S3: GT911 capacitive touch driver ---

// --- Shared touch state machine variables ---
#ifdef MECK_TOUCH_ENABLED
  static bool touchDown = false;
  static unsigned long touchDownTime = 0;
  static int16_t touchDownX = 0;
  static int16_t touchDownY = 0;
  static int16_t touchLastX = 0;
  static int16_t touchLastY = 0;
  static unsigned long lastTouchSeenMs = 0;
  #define TOUCH_LONG_PRESS_MS  750
  #if   defined(MECK_TWATCH)
    #define TOUCH_SWIPE_THRESHOLD 16   // Watch: getTouch() returns 0..119 (240px/UI_ZOOM); 16 is ~13% of width
  #else
    #define TOUCH_SWIPE_THRESHOLD 30   // T-Deck Pro: 240×320 — 30px ≈ 12.5% of width
  #endif
  #define TOUCH_LIFT_DEBOUNCE_MS 150
  #define TOUCH_MIN_INTERVAL_MS 300
  static bool longPressHandled = false;
  static bool swipeHandled = false;
  static bool touchCooldown = false;
  static unsigned long lastTouchEventMs = 0;


  // Unified touch reader — returns physical screen coordinates
  static bool readTouch(int16_t* outX, int16_t* outY) {
  #if   defined(MECK_TWATCH)
    {
      // FT6336U is read through the LovyanGFX panel backing the display.
      // display.getTouch() returns coordinates already divided by UI_ZOOM.
      int tx, ty;
      if (display.getTouch(&tx, &ty)) {
        *outX = (int16_t)tx;
        *outY = (int16_t)ty;
        return true;
      }
      return false;
    }
  #else
    return false;
  #endif
  }

  // Convert physical touch coords to virtual 128×128 coordinate space
  static void touchToVirtual(int16_t px, int16_t py, int& vx, int& vy) {
  #if   defined(MECK_TWATCH)
    // display.getTouch() already divides by UI_ZOOM, so px/py span the
    // 240/UI_ZOOM logical canvas (0..119 at UI_ZOOM=2). Scale to 128 virtual.
    float sx = (240.0f / UI_ZOOM) / 128.0f;
    float sy = (240.0f / UI_ZOOM) / 128.0f;
  #endif
    vx = (int)(px / sx);
    vy = (int)(py / sy);
  }
#endif

// Board-agnostic: CPU frequency scaling and AGC reset
CPUPowerManager cpuPower;
#define AGC_RESET_INTERVAL_MS 500
static unsigned long lastAGCReset = 0;

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

#if   defined(ESP32)
  #include <SPIFFS.h>
  #if defined(MECK_TWATCH)
    #include <LittleFS.h>   // watch: dedicated 'maps' tiles partition
  #endif
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef ESP32
  #ifdef WIFI_SSID
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(MECK_WIFI_COMPANION)
    #include <WiFi.h>
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(BLE_PIN_CODE)
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#else
  #error "need to define a serial interface"
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  #if defined(MECK_TWATCH)
    #include "WatchNotesScreen.h"  // After UITask.h -- needs NodePrefs
    #include "WatchChannelConfigScreen.h"  // After UITask.h -- needs NodePrefs, the_mesh
  #endif
  #if defined(MECK_TWATCH) && HAS_GPS
    #include "WatchMapScreen.h"  // After BLE -- PNGdec headers conflict with BLE if included earlier
  #endif
  UITask ui_task(&board, &serial_interface);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

/* END GLOBAL OBJECTS */

// ---------------------------------------------------------------------------
// Voice-over-LoRa: incoming raw packet handler (dz0ny VE3 protocol)
// Registered with the_mesh.setVoiceHandler() when voice screen is created.
// Called from onRawDataRecv for magic 0x56 (voice data) and 0x72 (fetch req).
// ---------------------------------------------------------------------------

// Last Heard: add/remove contact for selected entry.
// Called from both touch double-tap (mapTouchTap) and keyboard Enter handler.
#ifdef DISPLAY_CLASS
static void lastHeardToggleContact() {
  LastHeardScreen* lh = (LastHeardScreen*)ui_task.getLastHeardScreen();
  if (!lh) return;
  const AdvertPath* entry = lh->getSelectedEntry();
  if (!entry) return;

  ContactInfo* existing = the_mesh.lookupContactByPubKey(entry->pubkey_prefix, 8);
  if (existing) {
    // Double-confirm for favourites (bit 0 of flags)
    static unsigned long lastRemoveAttempt = 0;
    static uint8_t lastRemovePrefix[8] = {0};
    bool isFav = (existing->flags & 0x01) != 0;

    if (isFav) {
      if (millis() - lastRemoveAttempt < 3000 &&
          memcmp(lastRemovePrefix, entry->pubkey_prefix, 8) == 0) {
        // Second press within 3s — confirmed
      } else {
        // First press on favourite — warn and wait
        lastRemoveAttempt = millis();
        memcpy(lastRemovePrefix, entry->pubkey_prefix, 8);
        ui_task.showAlert("Favourite! Press again", 2500);
        return;
      }
    }

    the_mesh.removeContact(*existing);
    the_mesh.scheduleLazyContactSave();
    char alertBuf[40];
    snprintf(alertBuf, sizeof(alertBuf), "Removed: %s", entry->name);
    ui_task.showAlert(alertBuf, 1500);
    Serial.printf("[LastHeard] Removed: %s\n", entry->name);
  } else {
    uint8_t blob[256];
    int blobLen = the_mesh.getContactBlob(entry->pubkey_prefix, 8, blob);
    if (blobLen > 0) {
      the_mesh.forceImportContact(blob, blobLen);
      the_mesh.scheduleLazyContactSave();
      char alertBuf[40];
      snprintf(alertBuf, sizeof(alertBuf), "Added: %s", entry->name);
      ui_task.showAlert(alertBuf, 1500);
      Serial.printf("[LastHeard] Added: %s\n", entry->name);
    } else {
      // Blob store is limited to 100 entries — with many contacts, blobs
      // from non-contact nodes get evicted quickly. User needs to wait
      // for the node to re-broadcast its advert.
      ui_task.showAlert("Advert expired, try later", 2000);
      Serial.printf("[LastHeard] Blob evicted for %s (store full)\n", entry->name);
    }
  }
  ui_task.forceRefresh();
}
#endif

// Touch mapping — must be after ui_task declaration
#ifdef MECK_TOUCH_ENABLED

  // Map a single tap based on current screen context
  static char mapTouchTap(int16_t x, int16_t y) {
    // Convert physical screen coords to virtual (128×128)
    int vx, vy;
    touchToVirtual(x, y, vx, vy);

    // Dismiss boot navigation hint on any tap
    if (ui_task.isHintActive()) {
      ui_task.dismissBootHint();
      return 0;
    }

    // --- Status bar tap (top ~18 virtual units) → go home from any non-home screen ---
    // Exception: text reader reading mode uses full screen for content (no header)
    if (vy < 18 && !ui_task.isOnHomeScreen()) {
      if (ui_task.isOnTextReader()) {
        TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
        if (reader && reader->isReading()) {
          return 'd';  // reading mode: treat as next page
        }
      }
      ui_task.gotoHomeScreen();
      return 0;
    }

    // Games menu: tap to select game entry
    if (ui_task.isOnGamesMenu()) {
      GamesMenuScreen* gm = (GamesMenuScreen*)ui_task.getGamesMenuScreen();
      if (gm) {
        int result = gm->selectRowAtVY(vy);
        if (result == 2) return '\r';  // Tapped current selection -- launch
      }
      return 0;  // Cursor moved or miss -- just refresh
    }

    // Snake screen: tap = Enter (start / restart)
    if (ui_task.isOnSnakeScreen()) {
#if defined(MECK_TWATCH)
      // Watch: tap starts/restarts, or steers by tapping the up/down/left/right
      // zone relative to the board centre while playing.
      SnakeScreen* sk = (SnakeScreen*)ui_task.getSnakeScreen();
      if (sk) sk->handleTap(x, y);
      return 0;
#else
      return '\r';
#endif
    }

    // Minesweeper screen: tap = Enter (reveal cell / start / restart)
    if (ui_task.isOnMinesweeperScreen()) {
      return '\r';
    }

#if defined(MECK_TWATCH) && HAS_GPS
    // Map screen: footer +/- buttons zoom; a tap in the map area recenters on
    // GPS. (A top-strip tap is already handled above -> home.)
    if (ui_task.isOnMapScreen()) {
      WatchMapScreen* wms = (WatchMapScreen*)ui_task.getMapScreen();
      if (wms) wms->handleTap(x, y);
      return 0;
    }
#endif
#if defined(MECK_TWATCH)
    // Notes screen: list rows, view scroll zones and the footer are all tap
    // targets, in the same logical coords as the alarm screen.
    if (ui_task.isOnWatchNotesScreen()) {
      WatchNotesScreen* wn = (WatchNotesScreen*)ui_task.getWatchNotesScreen();
      if (wn) wn->handleTap(x, y);
      return 0;
    }
    // Channel config screen: list rows, detail action rows and the footer are
    // all tap targets, in the same logical coords as the alarm/notes screens.
    if (ui_task.isOnWatchChannelConfigScreen()) {
      WatchChannelConfigScreen* wc = (WatchChannelConfigScreen*)ui_task.getWatchChannelConfigScreen();
      if (wc) wc->handleTap(x, y);
      return 0;
    }
#endif
#ifdef TWATCH_COMPOSE_ENABLED
    // TWatch channel screen: touch is handled entirely in the screen's own
    // poll() (ticker select, hop-overlay dismiss, compose bar). Do not
    // synthesise keys here -- the hop-path overlay must open only on the
    // physical button (PWR on the S3 / boot on the Plus), whose KEY_ENTER /
    // KEY_NEXT arrive via a separate inject path.
    if (ui_task.isOnTWatchChannelScreen()) {
      return 0;
    }
#endif
#if defined(MECK_TWATCH)
    // Watch: tiles open on long-press and pages change on swipe, so a plain tap
    // on any home page does nothing -- skip the T5S3 tile hit-test below (wrong
    // geometry for the 2-column watch grid) and the left/right page cycling.
    if (ui_task.isOnHomeScreen()) {
      return 0;
    }
#endif
    // Home screen FIRST page: tile taps (virtual coordinate hit test)
    if (ui_task.isOnHomeScreen() && ui_task.isHomeShowingTiles()) {
      const int tileW = 40, tileH = 22, gapX = 1, gapY = 1;
      const int gridW = tileW * 3 + gapX * 2;
      const int gridX = (128 - gridW) / 2;  // =3
      int gridY = ui_task.getTileGridVY();

      // Check if tap is within the tile grid area
      if (vx >= gridX && vx < gridX + gridW &&
          vy >= gridY && vy < gridY + 3 * (tileH + gapY)) {
        int col = (vx - gridX) / (tileW + gapX);
        if (col > 2) col = 2;
        int row = (vy - gridY) / (tileH + gapY);
        if (row > 2) row = 2;

        if (row == 0 && col == 0) { ui_task.gotoChannelPickerScreen(); return 0; }
        if (row == 0 && col == 1) { ui_task.gotoContactsScreen(); return 0; }
        if (row == 0 && col == 2) { ui_task.gotoSettingsScreen(); return 0; }
        if (row == 1 && col == 0) { ui_task.gotoTextReader(); return 0; }
        if (row == 1 && col == 1) { ui_task.gotoNotesScreen(); return 0; }
#ifdef MECK_WEB_READER
        if (row == 1 && col == 2) { ui_task.gotoWebReader(); return 0; }
#else
        if (row == 1 && col == 2) { ui_task.gotoDiscoveryScreen(); return 0; }
#endif
        // Third row: Trace (col 0) + Games (col 1)
        if (row == 2 && col == 0) { ui_task.gotoTraceScreen(); return 0; }
        if (row == 2 && col == 1) { ui_task.gotoGamesMenu(); return 0; }
      }
      // Tap outside tiles — left half backward, right half forward
      return (vx < 64) ? (char)KEY_PREV : (char)KEY_NEXT;
    }

    // Home screen (non-tile pages): left half taps backward, right half forward
    // Exception: on Recent Adverts page, bottom area tap opens Last Heard
    if (ui_task.isOnHomeScreen()) {
      if (ui_task.isHomeOnRecentPage() && vy >= 100) {
        ui_task.gotoLastHeardScreen();
        return 0;
      }
      return (vx < 64) ? (char)KEY_PREV : (char)KEY_NEXT;
    }

    // Reader: tap in reading mode; in file list = select row
    if (ui_task.isOnTextReader()) {
      TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
      if (reader && reader->isReading()) {
        return 'd';  // Tap anywhere else = next page
      }
      // File list: tap-to-select, double-tap to open
      if (reader && reader->isInFileList()) {
        int result = reader->selectRowAtVY(vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;  // Moved selection
        }
        if (result == 2) return KEY_ENTER;  // Same row — open
      }
      return 0;
    }

    // Notes screen: mode-dependent touch
    if (ui_task.isOnNotesScreen()) {
      NotesScreen* notes = (NotesScreen*)ui_task.getNotesScreen();
      if (notes) {
        if (notes->isInFileList()) {
          int result = notes->selectRowAtVY(vy);
          if (result == 1) { ui_task.forceRefresh(); return 0; }  // Moved selection
          if (result == 2) return KEY_ENTER;  // Same row — open
          return 0;
        }
        if (notes->isEditing()) {
          return 0;  // T-Deck Pro: keyboard handles typing directly
        }
        // READING, RENAMING, CONFIRM_DELETE: tap = confirm/enter
        return KEY_ENTER;
      }
    }

#ifdef MECK_WEB_READER
    // Web reader: context-dependent taps
    if (ui_task.isOnWebReader()) {
      WebReaderScreen* wr = (WebReaderScreen*)ui_task.getWebReaderScreen();
      if (wr) {
        if (wr->isReading()) {
          return 'd';  // Tap reading area → next page
        }

        if (wr->isHome()) {
          return KEY_ENTER;  // Select current item (keyboard handles text on T-Deck Pro)
        }

        if (wr->isWifiSetup()) {
          return KEY_ENTER;  // SSID list: select, failed: retry
        }
      }
      return KEY_ENTER;
    }
#endif

    // Channel screen: tap footer area → hop path, tap elsewhere → no action
    if (ui_task.isOnChannelScreen()) {
      ChannelScreen* chScr = (ChannelScreen*)ui_task.getChannelScreen();
      if (chScr && chScr->isShowingPathOverlay()) {
        return 'q';  // Dismiss overlay on any tap
      }
      // Footer zone: bottom ~15 virtual units (≈63 physical pixels)
      if (vy >= 113) {
        return 'v';  // Show path overlay
      }
      return 0;  // Tap on message area — consumed, no action
    }

    // Channel picker screen: tap to select (T5S3: direct open, T-Deck Pro: highlight/activate)
    if (ui_task.isOnChannelPickerScreen()) {
      ChannelPickerScreen* pick = (ChannelPickerScreen*)ui_task.getChannelPickerScreen();
      if (pick) {
        int result = pick->selectAtVxVy(vx, vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;  // Moved cursor
        }
        if (result == 2) return KEY_ENTER;  // Activate — wantsExit checked after injectKey
      }
      return 0;
    }

    // Contacts screen: tap to select row, tap same row to activate
    // In select mode: tap same row toggles checkbox instead
    if (ui_task.isOnContactsScreen()) {
      ContactsScreen* cs = (ContactsScreen*)ui_task.getContactsScreen();
      if (cs) {
#if defined(MECK_TWATCH)
        if (cs->isActionOverlayOpen()) {
          int btn = cs->actionOverlayButtonAtVY(vy);
          if (btn == 0) {
            // Open -- route by contact type (mirrors the pre-overlay long-press open)
            cs->closeActionOverlay();
            int idx = cs->getSelectedContactIdx();
            uint8_t ctype = cs->getSelectedContactType();
            if (idx >= 0 && ctype == ADV_TYPE_CHAT) {
              // Always open the conversation view (sent + received history);
              // compose is a long-press inside the conversation, mirroring
              // the Meck-P4 flow. Passing idx lets the reply compose resolve
              // the recipient without a name lookup.
              char cname[32];
              cs->getSelectedContactName(cname, sizeof(cname));
              ui_task.clearDMUnread(idx);
              ui_task.gotoDMConversation(cname, idx);
              return 0;
            } else if (idx >= 0 && ctype == ADV_TYPE_REPEATER) {
              ui_task.gotoRepeaterAdmin(idx);
              return 0;
            } else if (idx >= 0 && ctype == ADV_TYPE_ROOM) {
              ui_task.gotoRepeaterAdmin(idx);
              return 0;
            } else if (idx >= 0 && ui_task.hasDMUnread(idx)) {
              char cname[32];
              cs->getSelectedContactName(cname, sizeof(cname));
              ui_task.clearDMUnread(idx);
              ui_task.gotoDMConversation(cname);
              return 0;
            }
            ui_task.forceRefresh();
            return 0;
          } else if (btn == 1) {
            // Favourite -- flip flags bit 0 on the selected contact and persist
            int idx = cs->getSelectedContactIdx();
            if (idx >= 0) {
              ContactInfo tmp;
              if (the_mesh.getContactByIdx(idx, tmp)) {
                ContactInfo* cp = the_mesh.lookupContactByPubKey(tmp.id.pub_key, PUB_KEY_SIZE);
                if (cp) {
                  cp->flags ^= 0x01;
                  the_mesh.saveContacts();
                  ui_task.showAlert((cp->flags & 0x01) ? "Favourited" : "Unfavourited", 1500);
                }
              }
            }
            cs->closeActionOverlay();
            cs->invalidateCache();
            ui_task.forceRefresh();
            return 0;
          } else if (btn == 2) {
            // Delete -- remove the selected contact and persist
            int idx = cs->getSelectedContactIdx();
            if (idx >= 0) {
              ContactInfo c;
              if (the_mesh.getContactByIdx(idx, c)) {
                if (the_mesh.removeContact(c)) {
                  the_mesh.saveContacts();
                  ui_task.showAlert("Contact deleted", 1500);
                }
              }
            }
            cs->closeActionOverlay();
            cs->invalidateCache();
            ui_task.forceRefresh();
            return 0;
          } else {
            // Tap outside the buttons -- dismiss
            cs->closeActionOverlay();
            ui_task.forceRefresh();
            return 0;
          }
        }
#endif
        int result = cs->selectRowAtVY(vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;  // Moved cursor
        }
        if (result == 2) {
          if (cs->isInSelectMode()) {
            cs->toggleSelected();
            ui_task.forceRefresh();
            return 0;
          }
          return KEY_ENTER;  // Normal mode — activate
        }
      }
      return 0;
    }

    // Path editor screen: tap to select row, tap same to activate
    if (ui_task.isOnPathEditor()) {
      PathEditorScreen* pe = (PathEditorScreen*)ui_task.getPathEditorScreen();
      if (pe) {
        int result = pe->selectRowAtVY(vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;
        }
        if (result == 2) return KEY_ENTER;
      }
      return 0;
    }

#if defined(MECK_TWATCH)
    // Trace screen (watch): tap to select row, tap same to activate. Mode row
    // toggles 1/2-byte; Type Path opens the keyboard (handleInput -> wantsKeyboard).
    // Guarded to the watch: selectRowAtVY uses the watch's 128-space coords, and
    // T5S3/T-Deck keep their existing boot-button/keyboard trace handling.
    if (ui_task.isOnTraceScreen()) {
      TraceScreen* ts = (TraceScreen*)ui_task.getTraceScreen();
      if (ts) {
        int result = ts->selectRowAtVY(vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;
        }
        if (result == 2) {
          if (ts->isOnModeRow()) return 'd';   // toggle 1-byte / 2-byte
          return KEY_ENTER;                     // Type Path / Add / Remove / Run / Exit / hop
        }
      }
      return 0;
    }
#endif

    // Discovery screen: tap to select, tap same to add
    if (ui_task.isOnDiscoveryScreen()) {
      DiscoveryScreen* ds = (DiscoveryScreen*)ui_task.getDiscoveryScreen();
      if (ds) {
        int result = ds->selectRowAtVY(vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;
        }
        if (result == 2) return KEY_ENTER;  // Same row — add to contacts
      }
      return 0;
    }

    // Last Heard screen: tap to select, tap same to add/remove
    if (ui_task.isOnLastHeardScreen()) {
      LastHeardScreen* lh = (LastHeardScreen*)ui_task.getLastHeardScreen();
      if (lh) {
        int result = lh->selectRowAtVY(vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;
        }
        if (result == 2) {
          lastHeardToggleContact();
          return 0;
        }
      }
      return 0;
    }

    // Settings screen: tap to select row, tap same row to activate
    if (ui_task.isOnSettingsScreen()) {
      SettingsScreen* ss = (SettingsScreen*)ui_task.getSettingsScreen();
#if defined(MECK_TWATCH)
      // Watch: while editing a number row (UTC / path hash / SF / CR), tap the
      // upper half to increment and the lower half to decrement. Long-press saves.
      if (ss && ss->isEditing() &&
          (ss->getCurrentRowType() == ROW_UTC_OFFSET ||
           ss->getCurrentRowType() == ROW_PATH_HASH_SIZE ||
           ss->getCurrentRowType() == ROW_SF ||
           ss->getCurrentRowType() == ROW_CR)) {
        return (vy < 64) ? 'w' : 's';
      }
      // Watch: while choosing a radio preset, tap upper = next, lower = previous.
      if (ss && ss->isEditing() && ss->getCurrentRowType() == ROW_RADIO_PRESET) {
        return (vy < 64) ? 'd' : 'a';
      }
#endif
      if (ss && !ss->isEditing()) {
        int result = ss->selectRowAtVY(vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;  // Moved cursor — just refresh, don't activate
        }
        if (result == 2) {
#if defined(MECK_TWATCH)
          // Watch: a tap only selects/cycles rows; activation and edit happen
          // on long-press (see mapTouchLongPress), matching the "Hold:Edit
          // Tap:Cycle" footer and preventing accidental edits.
          return 0;
#else
          return KEY_ENTER;  // Tapped same row — activate
#endif
        }
      }
      return KEY_ENTER;  // Editing mode or header/footer tap
    }


#if defined(MECK_TWATCH)
    // Repeater admin (watch): tap selects a menu row (tap again = activate);
    // param entry opens the keyboard for the value.
    if (ui_task.isOnRepeaterAdmin()) {
      RepeaterAdminScreen* admin = (RepeaterAdminScreen*)ui_task.getRepeaterAdminScreen();
      if (admin) {
        RepeaterAdminScreen::AdminState astate = admin->getState();
        if (astate == RepeaterAdminScreen::STATE_CATEGORY_MENU ||
            astate == RepeaterAdminScreen::STATE_COMMAND_MENU) {
          int result = admin->selectRowAtVY(vy);
          if (result == 1) { ui_task.forceRefresh(); return 0; }
          if (result == 2) return KEY_ENTER;   // tapped current row -- activate
          return 0;
        }
        if (astate == RepeaterAdminScreen::STATE_PARAM_ENTRY) {
          ui_task.openTWatchKeyboard(3, 0);     // tap opens keyboard for the value
          return 0;
        }
      }
    }
#endif

    // All other screens: tap = select
    return KEY_ENTER;
  }

  // Map a swipe direction to a key
  static char mapTouchSwipe(int16_t dx, int16_t dy) {
    bool horizontal = abs(dx) > abs(dy);

    // SMS screen: dedicated touch handler covers all interaction

#if defined(MECK_TWATCH) && HAS_GPS
    // Map screen: swipe pans the viewport (logical-pixel delta)
    if (ui_task.isOnMapScreen()) {
      WatchMapScreen* wms = (WatchMapScreen*)ui_task.getMapScreen();
      if (wms) wms->panByPixels(dx, dy);
      return 0;
    }
#endif

    // Snake screen: swipes control direction
    if (ui_task.isOnSnakeScreen()) {
      if (horizontal) {
        return (dx < 0) ? 'a' : 'd';
      } else {
        return (dy < 0) ? 'w' : 's';
      }
    }

    // Minesweeper screen: swipes move cursor
    if (ui_task.isOnMinesweeperScreen()) {
      if (horizontal) {
        return (dx < 0) ? 'a' : 'd';
      } else {
        return (dy < 0) ? 'w' : 's';
      }
    }

    // Games menu: vertical swipe scrolls list
    if (ui_task.isOnGamesMenu()) {
      if (!horizontal) {
        return (dy > 0) ? 's' : 'w';
      }
      return 0;
    }

    // Reader (reading mode): swipe left/right for page turn
    if (ui_task.isOnTextReader()) {
      TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
      if (reader && reader->isReading()) {
        if (horizontal) {
          return (dx < 0) ? 'd' : 'a';  // swipe left=next, right=prev
        }
        // Vertical swipe in reader: also page turn (natural scroll)
        return (dy > 0) ? 'd' : 'a';  // swipe down=next, up=prev
      }
    }

#ifdef MECK_WEB_READER
    // Web reader: page turn in reading mode, list scroll elsewhere
    if (ui_task.isOnWebReader()) {
      WebReaderScreen* wr = (WebReaderScreen*)ui_task.getWebReaderScreen();
      if (wr && wr->isReading()) {
        if (horizontal) {
          return (dx < 0) ? 'd' : 'a';  // swipe left=next page, right=prev
        }
        return (dy > 0) ? 'd' : 'a';  // swipe down=next, up=prev
      }
      // HOME / WIFI_SETUP / other: vertical swipe = scroll list
      if (!horizontal) {
        return (dy > 0) ? 's' : 'w';
      }
      return 0;  // Ignore horizontal swipes on non-reading modes
    }
#endif

    // Home screen: swipe left = next page, swipe right = previous page
    if (ui_task.isOnHomeScreen()) {
      if (horizontal) {
        return (dx < 0) ? (char)KEY_NEXT : (char)KEY_PREV;
      }
      // Shutdown page: vertical swipe toggles hibernate/power off
      if (ui_task.isHomeOnShutdownPage()) {
        return 'w';  // toggle (direction doesn't matter)
      }
      return (char)KEY_NEXT;  // vertical swipe = next page (default)
    }

    // Settings: horizontal swipe → a/d for picker/number editing
    if (ui_task.isOnSettingsScreen() && horizontal) {
      return (dx < 0) ? 'd' : 'a';  // swipe left=next option, right=prev
    }

    // Channel screen: horizontal swipe → a/d to open channel picker
    if (ui_task.isOnChannelScreen() && horizontal) {
      return (dx < 0) ? 'd' : 'a';  // swipe left/right opens channel picker
    }

    // Contacts screen: horizontal swipe → a/d to change filter
    if (ui_task.isOnContactsScreen() && horizontal) {
      return (dx < 0) ? 'd' : 'a';  // swipe left=next filter, right=prev
    }

    // All other screens: vertical swipe scrolls
    if (!horizontal) {
      return (dy > 0) ? 's' : 'w';  // swipe down=scroll down, up=scroll up
    }

    return 0;  // ignore horizontal swipes on non-applicable screens
  }

  // Map a long press to a key
  static char mapTouchLongPress(int16_t x, int16_t y) {
    // SMS screen: dedicated touch handler covers all interaction

#ifdef TWATCH_COMPOSE_ENABLED
    // TWatch channel screen: no synthesised keys (see mapTouchTap) -- a long
    // press on the header/messages must not open the hop-path overlay.
    if (ui_task.isOnTWatchChannelScreen()) return 0;
#endif

    // Snake screen: long press exits to games menu
    if (ui_task.isOnSnakeScreen()) {
      return 'q';
    }

    // Minesweeper screen: long press toggles flag on cursor cell
    if (ui_task.isOnMinesweeperScreen()) {
      return 'f';
    }

    // Games menu: long press exits to home
    if (ui_task.isOnGamesMenu()) {
      return 'q';
    }

#if defined(MECK_TWATCH)
    // Watch FIRST page: long-press on a coloured tile opens its screen.
    // The grid renders in the logical (display.width()) space; touchToVirtual
    // yields 128-space coords, so scale them back to render space to hit-test.
    if (ui_task.isOnHomeScreen() && ui_task.isHomeShowingTiles()) {
      int vx, vy;
      touchToVirtual(x, y, vx, vy);
      const int W = display.width();                       // logical width (120)
      int rvx = vx * W / 128;
      int rvy = vy * W / 128;
      const int cols = 2, rows = 4;
      const int tileW = 56, tileH = 24, gapX = 4, gapY = 2;
      const int gridW = tileW * cols + gapX * (cols - 1);  // 116
      const int gridX = (W - gridW) / 2;                   // 2
      int gridY = ui_task.getTileGridVY();
      if (rvx >= gridX && rvx < gridX + gridW &&
          rvy >= gridY && rvy < gridY + rows * (tileH + gapY)) {
        int col = (rvx - gridX) / (tileW + gapX);  if (col > 1) col = 1;
        int row = (rvy - gridY) / (tileH + gapY);  if (row > 3) row = 3;
        if (row == 0 && col == 0) { ui_task.openTWatchPicker(); return 0; }
        if (row == 0 && col == 1) { ui_task.gotoContactsScreen();      return 0; }
        if (row == 1 && col == 0) { ui_task.gotoSettingsScreen();      return 0; }
        if (row == 1 && col == 1) { ui_task.gotoDiscoveryScreen();     return 0; }
        if (row == 2 && col == 0) { ui_task.gotoTraceScreen();         return 0; }
#if defined(LILYGO_TWATCH_S3)
        // No GNSS on the plain S3, so this slot is the games launcher.
        if (row == 2 && col == 1) { ui_task.gotoGamesMenu(); return 0; }
#elif HAS_GPS
        if (row == 2 && col == 1) {
          // Maps: mark the tile FS ready (detectZoomRange in enter() needs it)
          // before opening; GPS centre + markers are populated in the main loop.
          WatchMapScreen* wms = (WatchMapScreen*)ui_task.getMapScreen();
          if (wms) wms->setMapsReady(LittleFS.exists("/tiles"));
          ui_task.gotoMapScreen();
          return 0;
        }
#endif
        if (row == 3 && col == 0) { ui_task.gotoNotesScreen();         return 0; }
        if (row == 3 && col == 1) { ui_task.gotoStepsScreen();         return 0; }
      }
      return 0;  // consume long-press on the tile page
    }
#endif
#if defined(MECK_TWATCH)
    // Steps screen: long press opens the 7-day history subscreen.
    // (On the S3 the PWR key's short press also delivers KEY_ENTER here.)
    if (ui_task.isOnStepsScreen()) {
      return (char)KEY_ENTER;
    }
#endif

    // Home screen: long press = activate current page action
    // (BLE toggle, send advert, hibernate, GPS toggle, etc.)
    if (ui_task.isOnHomeScreen()) {
      return (char)KEY_ENTER;
    }

    // Reader reading: long press = close book
    if (ui_task.isOnTextReader()) {
      TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
      if (reader && reader->isReading()) {
        return 'q';
      }
      return KEY_ENTER;  // file list: open
    }

#ifdef MECK_WEB_READER
    // Web reader: long press for back navigation
    if (ui_task.isOnWebReader()) {
      WebReaderScreen* wr = (WebReaderScreen*)ui_task.getWebReaderScreen();
      if (wr) {
        if (wr->isReading()) {
          return 'b';  // Back to previous page, or HOME if no history
        }
        if (wr->isHome()) {
          int sel = wr->getHomeSelected();
          int bmEnd = 3 + wr->getBookmarkCount();
          if (sel >= 3 && sel < bmEnd) {
            return '\b';  // Long press on bookmark → delete
          }
          return 'q';  // All others: exit web reader
        }
        if (wr->isWifiSetup()) {
          return 'q';  // Back from WiFi setup
        }
        if (wr->isIRCMode()) {
          return 'q';  // Back from IRC
        }
      }
      return 'q';  // Default: back out
    }
#endif

    // Channel screen: long press → compose to current channel (or DM actions on DM tab)
    if (ui_task.isOnChannelScreen()) {
      uint8_t chIdx = ui_task.getChannelScreenViewIdx();
      if (chIdx == 0xFF) {
        ChannelScreen* chScr = (ChannelScreen*)ui_task.getChannelScreen();
        if (chScr->isDMInboxMode()) {
          // Inbox mode: long press = open selected conversation (same as Enter)
          return '\r';
        }
#if defined(MECK_TWATCH)
        // Watch conversation mode: long press = compose reply on the touch
        // keyboard. ChannelScreen does not consume KEY_ENTER here and the
        // watch has no physical keyboard, so open TWKB_DM directly.
        int dmIdx = chScr->getDMContactIdx();
        if (dmIdx >= 0) {
          ui_task.openTWatchKeyboard(1, dmIdx);   // TWKB_DM
        }
        return 0;
#else
        // Conversation mode: long press = compose reply
        return KEY_ENTER;
#endif
      }
      return KEY_ENTER;  // T-Deck Pro: keyboard handles compose mode
    }

    // Contacts screen: long press
    //   T-Deck Pro: toggle select mode (DM/admin handled by keyboard Enter)
    //   T5S3: DM for chat contacts, admin for repeaters/rooms (no physical keyboard)
    //         If in select mode, long press exits it on both platforms.
    if (ui_task.isOnContactsScreen()) {
      ContactsScreen* cs = (ContactsScreen*)ui_task.getContactsScreen();
      if (cs) {
        if (cs->isInSelectMode()) {
          // Both platforms: long press exits select mode
          cs->exitSelectMode();
          ui_task.forceRefresh();
          Serial.println("Contacts: exited select mode (touch long press)");
          return 0;
        }
#if   defined(MECK_TWATCH)
        // Watch: long press raises the contact action overlay
        // ([Open] [Favourite] [Delete]). The tap handler dispatches the choice;
        // "Open" reproduces the type-aware DM/admin routing this branch used to do.
        cs->openActionOverlay();
        ui_task.forceRefresh();
        return 0;
#else
        // T-Deck Pro: long press enters select mode
        cs->enterSelectMode();
        ui_task.forceRefresh();
        Serial.println("Contacts: entered select mode (touch long press)");
        return 0;
#endif
      }
      return KEY_ENTER;
    }

    // Discovery screen: long press = rescan
    if (ui_task.isOnDiscoveryScreen()) {
      return 'f';
    }

    // Path editor: long press = Enter (select item)
    if (ui_task.isOnPathEditor()) {
      return KEY_ENTER;
    }

    // Trace screen: long press = Enter on most rows; on T5S3 the Type Path row
    // opens the virtual keyboard instead so users can enter a comma-separated
    // hash list without a physical keyboard.
    if (ui_task.isOnTraceScreen()) {
      return KEY_ENTER;
    }

    // Repeater admin: long press → open keyboard for password or CLI
    if (ui_task.isOnRepeaterAdmin()) {
      RepeaterAdminScreen* admin = (RepeaterAdminScreen*)ui_task.getRepeaterAdminScreen();
      if (admin) {
        RepeaterAdminScreen::AdminState astate = admin->getState();
        if (astate == RepeaterAdminScreen::STATE_PASSWORD_ENTRY) {
#if   defined(MECK_TWATCH)
          ui_task.openTWatchKeyboard(2, 0);   // TWKB_ADMIN_PASSWORD
          return 0;
#else
          return KEY_ENTER;  // T-Deck Pro: keyboard handles password entry
#endif
        }
#if defined(MECK_TWATCH)
        // Watch: keyboard only for param entry; menu/other states activate the
        // highlighted item (no raw CLI keyboard).
        if (astate == RepeaterAdminScreen::STATE_PARAM_ENTRY) {
          ui_task.openTWatchKeyboard(3, 0);   // param value entry
          return 0;
        }
        return KEY_ENTER;
#endif
      }
    }

    // Notes screen: long press in editor → save and exit
    if (ui_task.isOnNotesScreen()) {
      NotesScreen* notes = (NotesScreen*)ui_task.getNotesScreen();
      if (notes && notes->isEditing()) {
        notes->triggerSaveAndExit();
        return 0;
      }
    }

    // Settings screen: context-dependent long press
    if (ui_task.isOnSettingsScreen()) {
      SettingsScreen* ss = (SettingsScreen*)ui_task.getSettingsScreen();
      if (ss) {
#if defined(MECK_TWATCH)
        // Watch: long-press while editing a number row or the preset picker
        // saves the value.
        if (ss->isEditing() &&
            (ss->getCurrentRowType() == ROW_UTC_OFFSET ||
             ss->getCurrentRowType() == ROW_PATH_HASH_SIZE ||
             ss->getCurrentRowType() == ROW_SF ||
             ss->getCurrentRowType() == ROW_CR ||
             ss->getCurrentRowType() == ROW_RADIO_PRESET)) {
          return '\r';
        }
#endif
        if (ss->isEditing()) {
          return 0;  // Consume — don't interfere with active edit mode
        }
#if defined(MECK_TWATCH)
        // Aim activation at the row under the finger, not just the current
        // cursor, so a single long-press opens/edits the pressed row.
        { int vx, vy; touchToVirtual(x, y, vx, vy); ss->selectRowAtVY(vy); }
#endif
        if (ss->isOnDeletableChannel()) {
          return 'x';  // Long press on channel row → delete
        }
      }
      return KEY_ENTER;  // Not editing: toggle/edit selected row
    }

    // Channel picker: long press = delete message history for highlighted channel
    if (ui_task.isOnChannelPickerScreen()) {
      return 'x';
    }

    // Default: enter/select
    return KEY_ENTER;
  }
#endif

void halt() {
  while (1) ;
}

void setup() {
  Serial.begin(115200);
  delay(100);  // Give serial time to initialize
  MESH_DEBUG_PRINTLN("=== setup() - STARTING ===");

  board.begin();
  MESH_DEBUG_PRINTLN("setup() - board.begin() done");

  // Initialize touch input (CST328) HERE, immediately after board.begin(),
  // while the I2C bus is freshly initialised and quiet -- mirroring LilyGo's
  // working example, which calls touch.begin() first thing. Running it later
  // (after display/radio/filesystem init) left the bus in a state where the
  // CST328 firmware-info read failed.

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  MESH_DEBUG_PRINTLN("setup() - about to call display.begin()");
  
  // =========================================================================
  // T-Deck Pro V1.1: Initialize E-Ink reset pin BEFORE display.begin()
  // This is critical - the display won't work without proper reset sequence
  // =========================================================================
  // =========================================================================
  
  if (display.begin()) {
    MESH_DEBUG_PRINTLN("setup() - display.begin() returned true");
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
    MESH_DEBUG_PRINTLN("setup() - Loading screen drawn");
  } else {
    MESH_DEBUG_PRINTLN("setup() - display.begin() returned false!");
  }
#endif

  MESH_DEBUG_PRINTLN("setup() - about to call radio_init()");
  if (!radio_init()) { 
    MESH_DEBUG_PRINTLN("setup() - radio_init() FAILED! Halting.");
    halt(); 
  }
  MESH_DEBUG_PRINTLN("setup() - radio_init() done");

  // CPU frequency scaling -- drop to 80 MHz for idle mesh listening
  cpuPower.begin();

  MESH_DEBUG_PRINTLN("setup() - about to call fast_rng.begin()");
  fast_rng.begin(radio_get_rng_seed());
  MESH_DEBUG_PRINTLN("setup() - fast_rng.begin() done");

#if   defined(ESP32)
  MESH_DEBUG_PRINTLN("setup() - ESP32 filesystem init - calling SPIFFS.begin()");
  if (!SPIFFS.begin(false)) {
    // First boot or corrupted partition -- format required (can take 1-2 minutes)
    Serial.println("SPIFFS mount failed - formatting (this may take 1-2 minutes)...");
    if (disp) {
      disp->startFrame();
      disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
      disp->setTextSize(1);
      disp->setColor(DisplayDriver::YELLOW);
      disp->drawTextCentered(disp->width() / 2, 52, "Formatting storage...");
      disp->drawTextCentered(disp->width() / 2, 65, "First boot - please wait - may take 1-2 mins");
      disp->endFrame();
    }
    if (!SPIFFS.begin(true)) {
      // Auto-format failed -- partition likely contains non-SPIFFS data from
      // a previous firmware. Erase the entire partition and retry.
      Serial.println("SPIFFS auto-format failed -- erasing partition and retrying...");
      const esp_partition_t* spiffsPart = esp_partition_find_first(
          ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
      if (spiffsPart) {
        Serial.printf("SPIFFS partition: offset=0x%x size=0x%x -- erasing...\n",
                       spiffsPart->address, spiffsPart->size);
        esp_partition_erase_range(spiffsPart, 0, spiffsPart->size);
      }
      bool spiffsMounted = false;
      for (int attempt = 0; attempt < 3 && !spiffsMounted; attempt++) {
        if (attempt > 0) { Serial.printf("SPIFFS retry %d/3...\n", attempt + 1); delay(500); }
        SPIFFS.format();
        spiffsMounted = SPIFFS.begin(false);
      }
      if (spiffsMounted) {
        Serial.println("SPIFFS mounted after explicit format");
      } else {
        Serial.println("ERROR: SPIFFS mount failed after all retries");
      }
    } else {
      Serial.println("SPIFFS format complete");
    }
  }
  MESH_DEBUG_PRINTLN("setup() - SPIFFS.begin() done");

#if defined(MECK_TWATCH)
  // Mount the dedicated 'maps' partition as LittleFS (map tiles live here,
  // kept separate from the SPIFFS DataStore that holds identity/contacts).
  // formatOnFail=true so a freshly flashed (raw) maps partition is formatted
  // and immediately usable for ESPConnect tile uploads.
  if (LittleFS.begin(true, "/maps", 10, "maps")) {
    Serial.printf("maps: LittleFS mounted (%u KB total, %u KB used)\n",
                  (unsigned)(LittleFS.totalBytes() / 1024),
                  (unsigned)(LittleFS.usedBytes() / 1024));
  } else {
    Serial.println("maps: LittleFS mount FAILED");
  }
#endif

  // ---------------------------------------------------------------------------
  // Early SD card init -- needed BEFORE the_mesh.begin() so we can restore
  // settings from a previous firmware flash.  The display SPI bus is already
  // up (display.begin() ran earlier), so SD can share it now.
  // ---------------------------------------------------------------------------

  // Copy bundled notification sounds to SD card (audio variant only).
  // Skips files that already exist so user customisations are preserved.

  // Initialise per-channel notification sound config (audio + 4G variants).

  MESH_DEBUG_PRINTLN("setup() - about to call store.begin()");
  store.begin();
  MESH_DEBUG_PRINTLN("setup() - store.begin() done");
  
  MESH_DEBUG_PRINTLN("setup() - about to call the_mesh.begin()");
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
  MESH_DEBUG_PRINTLN("setup() - the_mesh.begin() done");


  // Boot-time config import: check for /meshcore/import.json on SD
  #ifdef HAS_SDCARD
  if (sdCardReady) {
    int importResult = meckImportConfig(the_mesh,
                                        sensors.node_lat, sensors.node_lon,
                                        sdCardReady);
    if (importResult > 0) {
      MESH_DEBUG_PRINTLN("setup() - config imported from SD");
    }
    // importResult == 0 means no file found (normal), -1 means error (logged inside)
    // If identity was changed, meckImportConfig already rebooted before returning.
  }
  #endif

  // Apply the persisted RX duty-cycle preference now that prefs are final
  // (after any SD config import). Takes effect once the duty machine is
  // compiled in (MECK_RX_DUTY_CYCLE); otherwise the flag is simply recorded.
  radio_driver.setDutyCycleEnabled(the_mesh.getNodePrefs()->rx_duty_cycle != 0);
  radio_driver.setDutyLevel(the_mesh.getNodePrefs()->rx_duty_level);

#ifdef WIFI_SSID
  MESH_DEBUG_PRINTLN("setup() - WiFi mode (compile-time credentials)");
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  serial_interface.begin(TCP_PORT);
#elif defined(MECK_WIFI_COMPANION)
  {
    // WiFi companion: load credentials from SD at runtime.
    // TCP server starts regardless — companion connects when WiFi comes up.
    MESH_DEBUG_PRINTLN("setup() - WiFi companion mode (runtime credentials)");
    WiFi.mode(WIFI_STA);
    if (sdCardReady) {
      File f = SD.open("/web/wifi.cfg", FILE_READ);
      if (f) {
        String ssid = f.readStringUntil('\n'); ssid.trim();
        String pass = f.readStringUntil('\n'); pass.trim();
        f.close();
        digitalWrite(SDCARD_CS, HIGH);
        if (ssid.length() > 0) {
          MESH_DEBUG_PRINTLN("setup() - WiFi: connecting to '%s'", ssid.c_str());
          WiFi.begin(ssid.c_str(), pass.c_str());
          unsigned long timeout = millis() + 3000;  // 3s max — non-critical, Settings can retry
          while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
            yield();  // Feed WDT during wait
            delay(50);
          }
          if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("WiFi companion: connected to %s, IP: %s\n",
                          ssid.c_str(), WiFi.localIP().toString().c_str());
          } else {
            Serial.println("WiFi companion: auto-connect failed (configure in Settings)");
          }
        }
      } else {
        digitalWrite(SDCARD_CS, HIGH);
        Serial.println("WiFi companion: no /web/wifi.cfg found (configure in Settings)");
      }
    }
    serial_interface.begin(TCP_PORT);
    MESH_DEBUG_PRINTLN("setup() - WiFi TCP server started on port %d", TCP_PORT);
  }
#elif defined(BLE_PIN_CODE)
  MESH_DEBUG_PRINTLN("setup() - about to call serial_interface.begin() with BLE");
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  MESH_DEBUG_PRINTLN("setup() - serial_interface.begin() done");
#elif defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  serial_interface.begin(companion_serial);
#else
  serial_interface.begin(Serial);
#endif
  MESH_DEBUG_PRINTLN("setup() - about to call the_mesh.startInterface()");
  the_mesh.startInterface(serial_interface);
  MESH_DEBUG_PRINTLN("setup() - the_mesh.startInterface() done");

#else
  #error "need to define filesystem"
#endif

  MESH_DEBUG_PRINTLN("setup() - about to call sensors.begin()");
  sensors.begin();
  MESH_DEBUG_PRINTLN("setup() - sensors.begin() done");

  // IMPORTANT: sensors.begin() calls initBasicGPS() which steals the GPS pins for Serial1.
  // We must end Serial1 first, then reclaim the pins for Serial2 (which feeds gpsStream).
  // This is ESP32-specific — on nRF52, GPS Serial1 is initialised in radio_init().
  #if HAS_GPS && defined(ESP32)
    Serial1.end();   // Release GPS pins from Serial1's UART + ISR
    Serial2.end();   // Close any existing Serial2
    {
      uint32_t gps_baud = the_mesh.getNodePrefs()->gps_baudrate;
      Serial.printf("GPS: prefs gps_baudrate=%lu (0=use default)\n", (unsigned long)gps_baud);
      if (gps_baud == 0) gps_baud = GPS_BAUDRATE;
      Serial2.begin(gps_baud, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
      Serial.printf("GPS: Serial2 started at %lu baud (RX=%d TX=%d)\n",
                     (unsigned long)gps_baud, GPS_RX_PIN, GPS_TX_PIN);
    }
  #endif

#ifdef DISPLAY_CLASS
  MESH_DEBUG_PRINTLN("setup() - about to call ui_task.begin()");
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());
  MESH_DEBUG_PRINTLN("setup() - ui_task.begin() done");

  // T-Echo Card: replace the generic HomeScreen with the 72x40 OLED home screen
#endif

  // ---------------------------------------------------------------------------
  // OTA boot validation — confirm new firmware is working after an OTA update.
  // If we reach this point, display + radio + SD + mesh all initialised OK.
  // Without this call (when CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is set),
  // the bootloader will roll back to the previous partition on next reboot.
  // ---------------------------------------------------------------------------
  #ifdef MECK_OTA_UPDATE
  {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
      if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
          Serial.println("OTA: New firmware validated, rollback cancelled");
        } else {
          Serial.println("OTA: WARNING - failed to cancel rollback");
        }
      }
    }
  }
  #endif

  // Initialize T-Deck Pro keyboard

  // Touch input (CST328) is initialised earlier, right after board.begin(),
  // on a freshly-initialised quiet I2C bus (see setup() near board.begin()).

  // Initialize GT911 touch (T5S3 E-Paper Pro)
  // Wire is already initialized by T5S3Board::begin(). The 4-arg begin() re-calls
  // Wire.begin() which logs "bus already initialized" — cosmetic only, not harmful.

  // Initialize CardKB external keyboard (if connected via QWIIC)

  // RTC diagnostic + boot-time serial clock sync
  // Works on all Meck builds — T-Deck Pro has no hardware RTC and GPS may
  // not fix immediately; T5S3 has hardware RTC but it needs initial setting.
  // Now set up SD-dependent features: message history + text reader.
  // ---------------------------------------------------------------------------

  // T5S3 SD-dependent features
#if defined(MECK_TWATCH)
  // Watch: load persisted channel-message history from LittleFS (no SD).
  {
    ChannelScreen* chanScr = (ChannelScreen*)ui_task.getChannelScreen();
    if (chanScr && chanScr->loadFromLittleFS()) {
      MESH_DEBUG_PRINTLN("setup() - Message history loaded from LittleFS");
    }
  }
#endif
  // Check if node name is still the default hex prefix (first 4 bytes of pub key)
  // If so, launch onboarding wizard to set name and radio preset
  // ---------------------------------------------------------------------------

  // GPS power — honour saved pref, default to enabled on first boot.
  // GPS is critical for timesync on standalone variants without 4G.
  #if HAS_GPS
  {
    bool gps_wanted = the_mesh.getNodePrefs()->gps_enabled;
    Serial.printf("GPS: pref gps_enabled=%d\n", (int)gps_wanted);
    if (gps_wanted) {
      #ifdef PIN_GPS_EN
        digitalWrite(PIN_GPS_EN, GPS_EN_ACTIVE);
      #endif
      #if defined(MECK_TWATCH)
        board.gpsPowerOn();   // GPS power is the AXP2101 BLDO1 rail
      #endif
      sensors.setSettingValue("gps", "1");
    } else {
      #ifdef PIN_GPS_EN
        digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);
      #endif
      #if defined(MECK_TWATCH)
        board.gpsPowerOff();  // GPS power is the AXP2101 BLDO1 rail
      #endif
      sensors.setSettingValue("gps", "0");
    }
    Serial.printf("GPS: power %s\n", gps_wanted ? "ON" : "OFF");
  }
  #endif

  // BLE starts disabled for standalone-first operation
  // User can toggle it from the Bluetooth home page (Enter or long-press)

  // Alarm clock: create at boot so config is loaded, background alarm check
  // works from first loop(), and the bell indicator is visible immediately.
  // Audio object is NOT created here — lazy-init when alarm fires or user opens player.

  // Register voice-over-LoRa callbacks early so incoming VE3 envelopes and
  // raw voice packets are handled even before user opens the voice screen.
  // The callbacks null-check the voice screen pointer, so they're safe at boot.

#ifdef ESP32
  Serial.printf("setup() complete - free heap: %d, largest block: %d\n",
                 ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
  {
    void* p25 = malloc(25000); void* p10 = malloc(10000); void* p4 = malloc(4000);
    Serial.printf("setup() heap probe: 25K=%s 10K=%s 4K=%s\n",
                  p25?"yes":"no", p10?"yes":"no", p4?"yes":"no");
    if (p4) free(p4); if (p10) free(p10); if (p25) free(p25);
  }
  MESH_DEBUG_PRINTLN("=== setup() - COMPLETE ===");
}

// ---------------------------------------------------------------------------
// OTA radio control — pause LoRa during firmware updates to prevent SPI
// bus contention (SD and LoRa share the same SPI bus on both platforms).
// Also pauses the mesh loop to prevent radio state confusion while standby.
// ---------------------------------------------------------------------------
#ifdef MECK_OTA_UPDATE
extern RADIO_CLASS radio;  // Defined in target.cpp

static bool otaRadioPaused = false;

void otaPauseRadio() {
  otaRadioPaused = true;
  radio.standby();
  Serial.println("OTA: Radio standby, mesh loop paused");
}

void otaResumeRadio() {
  radio.startReceive();
  otaRadioPaused = false;
  Serial.println("OTA: Radio receive resumed, mesh loop active");
}
#endif

void loop() {
#if defined(MECK_TWATCH)
  // Watch: flush pending channel-message history to LittleFS on a 30s debounce
  // (saveToSD marks it dirty on each message; at most one ~57 KB write / 30s).
  {
    ChannelScreen* chanScr = (ChannelScreen*)ui_task.getChannelScreen();
    if (chanScr) chanScr->flushMessagesIfDue(30000UL);
  }
#endif
  // T-Echo Card: lazy Codec2 init from shallow stack context.
  // codec2_create needs ~3KB stack for FFT/trig init. The loop task
  // has only 4KB total. Calling from render() (deep call chain) overflows.
  // Calling from here (top of loop) works because stack depth is minimal.

  #ifdef MECK_OTA_UPDATE
  if (!otaRadioPaused) {
  #endif


  the_mesh.loop();

  #ifdef MECK_OTA_UPDATE
  } else {
    // OTA/File Manager active — poll the web server from the main loop for fast response.
    // The render cycle on T5S3 (960×540 FastEPD) can block for 500ms+ during
    // e-ink refresh, causing the browser to timeout before handleClient() runs.
    // Polling here gives us ~1-5ms response time instead.
    if (ui_task.isOnSettingsScreen()) {
      SettingsScreen* ss = (SettingsScreen*)ui_task.getSettingsScreen();
      if (ss) {
        ss->pollOTAServer();
        // Detect upload completion and trigger verify → flash → reboot.
        // Must happen here (not in render) because T5S3 e-ink refresh blocks
        // for 500ms+ and the render-based check never fires reliably.
        ss->checkOTAComplete(display);
      }
    }
  }
  #endif


  sensors.loop();

  // Satellite-count diagnostic (MAX): every 10s while GPS is on, print the
  // reported satellite count, fix state and NMEA rate so the multi-constellation
  // change can be watched climbing. Remove once confirmed if you don't want it.

  // GPS diagnostic — disabled to reduce serial noise (uncomment for debugging)
  // #if HAS_GPS
  // {
  //   static unsigned long lastGpsDiag = 0;
  //   if (millis() - lastGpsDiag >= 30000) {
  //     lastGpsDiag = millis();
  //     uint32_t sentences = gpsStream.getSentenceCount();
  //     uint16_t perSec = gpsStream.getSentencesPerSec();
  //     Serial.printf("GPS diag: %lu sentences total, %u/sec, Serial2.available=%d, lat=%.6f lon=%.6f\n",
  //                   (unsigned long)sentences, perSec, Serial2.available(),
  //                   sensors.node_lat, sensors.node_lon);
  //   }
  // }
  // #endif

  // Map screen: periodically update own GPS position and contact markers
  #ifdef DISPLAY_CLASS
  #endif

#if defined(MECK_TWATCH) && HAS_GPS
  // Watch map screen: on open, centre on GPS and populate contact markers; then
  // refresh own position + markers periodically while it stays open.
  {
    static bool mapWasOpen = false;
    static unsigned long lastMapUpdate = 0;
    if (ui_task.isOnMapScreen()) {
      WatchMapScreen* wms = (WatchMapScreen*)ui_task.getMapScreen();
      if (wms) {
        bool justOpened = !mapWasOpen;
        mapWasOpen = true;
        if (justOpened || (millis() - lastMapUpdate > 30000)) {
          lastMapUpdate = millis();
          if (justOpened) {
            wms->setGPSPosition(sensors.node_lat, sensors.node_lon);
          } else {
            wms->updateGPSPosition(sensors.node_lat, sensors.node_lon);
          }
          wms->clearMarkers();
          ContactsIterator it = the_mesh.startContactsIterator();
          ContactInfo ci;
          while (it.hasNext(&the_mesh, ci)) {
            if (ci.gps_lat != 0 || ci.gps_lon != 0) {
              double lat = ((double)ci.gps_lat) / 1000000.0;
              double lon = ((double)ci.gps_lon) / 1000000.0;
              wms->addMarker(lat, lon, ci.name, ci.type);
            }
          }
        }
      }
    } else {
      mapWasOpen = false;
    }
  }
#endif

  // CPU frequency auto-timeout back to idle
  cpuPower.loop();

  // Low-power mode — drop CPU to 40 MHz and throttle loop when lock screen
  // is active.  The mesh radio has its own FIFO so packets are buffered;
  // 50 ms yield means the loop still runs 20×/sec which is more than enough
  // to drain the radio FIFO before overflow.
#if defined(MECK_TWATCH)
  {
    static bool wasLocked = false;
    bool nowLocked = ui_task.isLocked();
    if (nowLocked && !wasLocked) {
      cpuPower.setLowPower();
      Serial.printf("[Power] Low-power mode: CPU %d MHz, loop throttled\n",
                    cpuPower.getFrequencyMHz());
    } else if (!nowLocked && wasLocked) {
      cpuPower.clearLowPower();
      Serial.printf("[Power] Normal mode: CPU %d MHz\n",
                    cpuPower.getFrequencyMHz());
    }
    wasLocked = nowLocked;
  }
#endif

  // Audiobook: service audio decode regardless of which screen is active

  // Alarm clock: background alarm check + audio tick

  // Notification tone: play custom MP3 for per-channel notification sounds.
  // Polls the notifSounds pending request, lazy-inits Audio*, plays the file,
  // and disables DAC when playback finishes.

  // Voice message: service mic DMA capture + playback audio decode

  // SMS: poll for incoming messages from modem
#ifdef DISPLAY_CLASS
  // Skip UITask rendering when in compose mode to prevent flickering
  ui_task.loop();
#endif
  rtc_clock.tick();
  // Periodic AGC reset - re-assert boosted RX gain to prevent sensitivity drift
  #ifdef MECK_OTA_UPDATE
  if (!otaRadioPaused)
  #endif
  if ((millis() - lastAGCReset) >= AGC_RESET_INTERVAL_MS) {
    radio_reset_agc();
    lastAGCReset = millis();
  }
  // Handle T-Deck Pro keyboard input

  // ---------------------------------------------------------------------------
  // Touch Input — tap/swipe/long-press state machine (T5S3 + T-Deck Pro)
  // Gestures:
  //   Tap = finger down + up with minimal movement → select/open
  //   Swipe = finger drag > threshold → scroll/page turn
  //   Long press = finger held > 750ms without moving → edit/enter
  // After processing an event, cooldown waits for finger lift before next event.
  // Touch is disabled while lock screen is active.
  // When virtual keyboard is active (T5S3), taps route to keyboard.
  // ---------------------------------------------------------------------------
  #ifdef MECK_TOUCH_ENABLED
  {
    // Guard: skip touch when locked or VKB active
    bool touchBlocked = ui_task.isLocked();

    if (!touchBlocked)
    {
      int16_t tx, ty;
      bool gotPoint = readTouch(&tx, &ty);
      unsigned long now = millis();

      if (gotPoint) {
        lastTouchSeenMs = now;
      }


      bool fingerPresent = (now - lastTouchSeenMs) < TOUCH_LIFT_DEBOUNCE_MS;

      if (touchCooldown) {
        if (!fingerPresent && (now - lastTouchEventMs) >= TOUCH_MIN_INTERVAL_MS) {
          touchCooldown = false;
          touchDown = false;
        }
      }
      else if (gotPoint && !touchDown) {
        touchDown = true;
        touchDownTime = now;
        touchDownX = tx;
        touchDownY = ty;
        touchLastX = tx;
        touchLastY = ty;
        longPressHandled = false;
        swipeHandled = false;
      }
      else if (touchDown && fingerPresent) {
        if (gotPoint) {
          touchLastX = tx;
          touchLastY = ty;
        }

        int16_t dx = touchLastX - touchDownX;
        int16_t dy = touchLastY - touchDownY;
        int16_t dist = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

        if (!swipeHandled && !longPressHandled && dist >= TOUCH_SWIPE_THRESHOLD) {
          swipeHandled = true;
          char c = mapTouchSwipe(dx, dy);
          if (c) {
            // Channel screen: horizontal swipe opens picker instead of cycling
            if (ui_task.isOnChannelScreen() && (c == 'a' || c == 'd')) {
              ui_task.gotoChannelPickerScreen();
#if defined(MECK_TWATCH)
            } else if (ui_task.isOnChannelScreen() && (c == 'w' || c == 's')) {
              // Watch: a vertical swipe pages the message view (one page/swipe).
              ChannelScreen* cs = (ChannelScreen*)ui_task.getChannelScreen();
              if (cs) { cs->pageScroll(c == 'w'); ui_task.forceRefresh(); }
            } else if (ui_task.isOnWatchNotesScreen() && (c == 'w' || c == 's')) {
              // Watch: a vertical swipe pages the notes list (one page/swipe).
              WatchNotesScreen* wn = (WatchNotesScreen*)ui_task.getWatchNotesScreen();
              if (wn) { wn->pageScroll(c == 'w'); ui_task.forceRefresh(); }
            } else if (ui_task.isOnSettingsScreen() && (c == 'w' || c == 's')) {
              // Watch: a vertical swipe pages the settings list (one page/swipe);
              // while editing a value, keep w/s for adjust.
              SettingsScreen* ss = (SettingsScreen*)ui_task.getSettingsScreen();
              if (ss && !ss->isEditing()) { ss->pageScroll(c == 'w'); ui_task.forceRefresh(); }
              else ui_task.injectKey(c);
#endif
            } else {
              ui_task.injectKey(c);
            }
            cpuPower.setBoost();
          }
          lastTouchEventMs = now;
          touchCooldown = true;
        }
        else if (!longPressHandled && !swipeHandled && dist < TOUCH_SWIPE_THRESHOLD &&
                 (now - touchDownTime) >= TOUCH_LONG_PRESS_MS) {
          longPressHandled = true;
          char c = mapTouchLongPress(touchDownX, touchDownY);
          if (c) {
            ui_task.injectKey(c);
            // Path editor: check if Save & Exit was triggered
            if (ui_task.isOnPathEditor()) {
              PathEditorScreen* pe = (PathEditorScreen*)ui_task.getPathEditorScreen();
              if (pe && pe->wantsExit()) {
                ui_task.gotoContactsScreen();
              }
            }
            // Trace screen: check if Exit was triggered
            if (ui_task.isOnTraceScreen()) {
              TraceScreen* ts = (TraceScreen*)ui_task.getTraceScreen();
              if (ts && ts->wantsExit()) {
                ui_task.gotoHomeScreen();
              }
            }
            // Snake screen: check if Exit was triggered
            if (ui_task.isOnSnakeScreen()) {
              SnakeScreen* ss = (SnakeScreen*)ui_task.getSnakeScreen();
              if (ss && ss->wantsExit()) {
                ui_task.gotoGamesMenu();
              }
            }
            // Minesweeper screen: check if Exit was triggered
            if (ui_task.isOnMinesweeperScreen()) {
              MinesweeperScreen* ms = (MinesweeperScreen*)ui_task.getMinesweeperScreen();
              if (ms && ms->wantsExit()) {
                ui_task.gotoGamesMenu();
              }
            }
            // Games menu: check if game launch was triggered
            if (ui_task.isOnGamesMenu()) {
              GamesMenuScreen* gm = (GamesMenuScreen*)ui_task.getGamesMenuScreen();
              if (gm && gm->wantsLaunch()) {
                GameID sel = gm->selectedGame();
                gm->clearFlags();
                switch (sel) {
                  case GAME_SNAKE: ui_task.gotoSnakeScreen(); break;
                  case GAME_MINESWEEPER: ui_task.gotoMinesweeperScreen(); break;
                  default: break;
                }
              }
            }
            // Channel picker: check if long-press Enter was handled (wantsExit)
            if (ui_task.isOnChannelPickerScreen()) {
              ChannelPickerScreen* pick = (ChannelPickerScreen*)ui_task.getChannelPickerScreen();
              if (pick && pick->wantsExit()) {
                ui_task.gotoChannelScreen(false);
              }
            }
            cpuPower.setBoost();
          }
          lastTouchEventMs = now;
          touchCooldown = true;
        }
      }
      else if (touchDown && !fingerPresent) {
        touchDown = false;
        if (!longPressHandled && !swipeHandled) {
          char c = mapTouchTap(touchDownX, touchDownY);
          ui_task.keepAlive();   // a tap keeps the display awake even if it produces no key
          if (c) {
            ui_task.injectKey(c);
            // Path editor: check if Save & Exit was triggered
            if (ui_task.isOnPathEditor()) {
              PathEditorScreen* pe = (PathEditorScreen*)ui_task.getPathEditorScreen();
              if (pe && pe->wantsExit()) {
                ui_task.gotoContactsScreen();
              }
            }
            // Trace screen: check if Exit was triggered
            if (ui_task.isOnTraceScreen()) {
              TraceScreen* ts = (TraceScreen*)ui_task.getTraceScreen();
              if (ts && ts->wantsExit()) {
                ui_task.gotoHomeScreen();
              }
            }
            // Snake screen: check if Exit was triggered
            if (ui_task.isOnSnakeScreen()) {
              SnakeScreen* ss = (SnakeScreen*)ui_task.getSnakeScreen();
              if (ss && ss->wantsExit()) {
                ui_task.gotoGamesMenu();
              }
            }
            // Minesweeper screen: check if Exit was triggered
            if (ui_task.isOnMinesweeperScreen()) {
              MinesweeperScreen* ms = (MinesweeperScreen*)ui_task.getMinesweeperScreen();
              if (ms && ms->wantsExit()) {
                ui_task.gotoGamesMenu();
              }
            }
            // Games menu: check if game launch was triggered
            if (ui_task.isOnGamesMenu()) {
              GamesMenuScreen* gm = (GamesMenuScreen*)ui_task.getGamesMenuScreen();
              if (gm && gm->wantsLaunch()) {
                GameID sel = gm->selectedGame();
                gm->clearFlags();
                switch (sel) {
                  case GAME_SNAKE: ui_task.gotoSnakeScreen(); break;
                  case GAME_MINESWEEPER: ui_task.gotoMinesweeperScreen(); break;
                  default: break;
                }
              }
            }
            // Channel picker: check if Enter/Shift+Del was handled (wantsExit)
            if (ui_task.isOnChannelPickerScreen()) {
              ChannelPickerScreen* pick = (ChannelPickerScreen*)ui_task.getChannelPickerScreen();
              if (pick && pick->wantsExit()) {
                ui_task.gotoChannelScreen(false);
              }
            }
          }
          cpuPower.setBoost();
          lastTouchEventMs = now;
          touchCooldown = true;
        }
      }
    }
  }

  // Virtual keyboard touch routing (T5S3 only — T-Deck Pro uses physical keyboard)
  #endif // MECK_TOUCH_ENABLED

  // ---------------------------------------------------------------------------
  // CardKB external keyboard polling (via QWIIC)
  // When VKB is active: typed characters feed into the VKB text buffer.
  // When VKB is not active: navigation keys route through injectKey().
  // ESC key maps to 'q' (back) when no VKB is active.
  // ---------------------------------------------------------------------------

  // Poll touch input for phone dialer numpad
  // Hybrid debounce: finger-up detection + 150ms minimum between accepted taps.
  // The CST328 INT pin is pulse-based (not level), so getPoint() can return
  // false intermittently during a hold. Time guard prevents that from
  // causing repeat fires.

  // Low-power loop throttle -- yield CPU when lock screen is active.
  // The RTOS idle task executes WFI (wait-for-interrupt) during delay(),
  // dramatically reducing CPU power draw.  50 ms gives 20 loop cycles/sec
  // which is ample for LoRa packet reception (radio has hardware FIFO).
#if defined(MECK_TWATCH)
  if (ui_task.isLocked()) {
    delay(50);
  }
#endif
}