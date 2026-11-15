#include "UITask.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>

#if defined(MECK_TWATCH)
  #include <target.h>   // display (touch), user_btn (PWR key)
#endif

#define AUTO_OFF_MILLIS      20000  // 20 seconds
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds

#if defined(MECK_TWATCH)
  // Boot Other Slot button, in the 120x120 logical canvas (240 px / UI_ZOOM).
  #define SLOT_BTN_X           4
  #define SLOT_BTN_Y           92
  #define SLOT_BTN_W           112
  #define SLOT_BTN_H           22
  #define SLOT_CONFIRM_MILLIS  5000   // the confirming second tap must land within this
  #define TOUCH_POLL_MILLIS    30
  // A finger counts as lifted only after this many empty polls in a row. The
  // FT6336 can drop samples mid-hold; without this a single long touch could
  // read as two taps and confirm the slot switch by itself.
  #define TOUCH_LIFT_POLLS     5
#endif

// 'meshcore', 128x13px
static const uint8_t meshcore_logo [] PROGMEM = {
    0x3c, 0x01, 0xe3, 0xff, 0xc7, 0xff, 0x8f, 0x03, 0x87, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 
    0x3c, 0x03, 0xe3, 0xff, 0xc7, 0xff, 0x8e, 0x03, 0x8f, 0xfe, 0x3f, 0xfe, 0x1f, 0xff, 0x1f, 0xfe, 
    0x3e, 0x03, 0xc3, 0xff, 0x8f, 0xff, 0x0e, 0x07, 0x8f, 0xfe, 0x7f, 0xfe, 0x1f, 0xff, 0x1f, 0xfc, 
    0x3e, 0x07, 0xc7, 0x80, 0x0e, 0x00, 0x0e, 0x07, 0x9e, 0x00, 0x78, 0x0e, 0x3c, 0x0f, 0x1c, 0x00, 
    0x3e, 0x0f, 0xc7, 0x80, 0x1e, 0x00, 0x0e, 0x07, 0x1e, 0x00, 0x70, 0x0e, 0x38, 0x0f, 0x3c, 0x00, 
    0x7f, 0x0f, 0xc7, 0xfe, 0x1f, 0xfc, 0x1f, 0xff, 0x1c, 0x00, 0x70, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x1f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x3f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x1e, 0x3f, 0xfe, 0x3f, 0xf0, 
    0x77, 0x3b, 0x87, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xfc, 0x38, 0x00, 
    0x77, 0xfb, 0x8f, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xf8, 0x38, 0x00, 
    0x73, 0xf3, 0x8f, 0xff, 0x0f, 0xff, 0x1c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x78, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfe, 0x3c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x3c, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfc, 0x3c, 0x0e, 0x1f, 0xf8, 0xff, 0xf8, 0x70, 0x3c, 0x7f, 0xf8, 
};

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
  _display->turnOn();

  char *version = strdup(firmware_version);
  char *dash = strchr(version, '-');
  if (dash) *dash = 0;

#if defined(MECK_TWATCH)
  // 120 px wide canvas: version and build date go on separate lines.
  strncpy(_version_info, version, sizeof(_version_info) - 1);
  _version_info[sizeof(_version_info) - 1] = 0;
  strncpy(_build_date, build_date, sizeof(_build_date) - 1);
  _build_date[sizeof(_build_date) - 1] = 0;

  _next_touch = 0;
  _touch_down = false;
  _touch_empty_polls = 0;
  _home_shown = false;
  _slot_armed = false;
  _slot_armed_until = 0;
  _boot_other_slot_requested = false;
  user_btn.begin();
#else
  snprintf(_version_info, sizeof(_version_info), "%s (%s)", version, build_date);
#endif
  free(version);
}

void UITask::renderCurrScreen() {
  char tmp[80];
  if (millis() < BOOT_SCREEN_MILLIS) {
    // Boot screen: logo + version
    _display->setColor(DisplayDriver::BLUE);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    _display->setColor(DisplayDriver::LIGHT);
    _display->setTextSize(1);
#if defined(MECK_TWATCH)
    // Same layout as the companion splash: if the version string is wider than
    // the canvas, split at the last space so the version number gets its own line.
    const char* sp = strrchr(_version_info, ' ');
    if (sp && _display->getTextWidth(_version_info) > _display->width()) {
      char name_part[48];
      int name_len = sp - _version_info;
      if (name_len >= (int)sizeof(name_part)) name_len = sizeof(name_part) - 1;
      memcpy(name_part, _version_info, name_len);
      name_part[name_len] = 0;
      _display->drawTextCentered(_display->width() / 2, 22, name_part);
      _display->drawTextCentered(_display->width() / 2, 32, sp + 1);
    } else {
      _display->drawTextCentered(_display->width() / 2, 22, _version_info);
    }
    _display->drawTextCentered(_display->width() / 2, 42, _build_date);
    _display->drawTextCentered(_display->width() / 2, 55, "< Repeater >");
    _home_shown = false;
#else
    uint16_t versionWidth = _display->getTextWidth(_version_info);
    _display->setCursor((_display->width() - versionWidth) / 2, 22);
    _display->print(_version_info);

    const char* node_type = "< Repeater >";
    uint16_t typeWidth = _display->getTextWidth(node_type);
    _display->setCursor((_display->width() - typeWidth) / 2, 35);
    _display->print(node_type);
#endif
  } else {
    // Home screen: node info
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::GREEN);
    _display->print(_node_prefs->node_name);

    _display->setCursor(0, 20);
    _display->setColor(DisplayDriver::YELLOW);
    sprintf(tmp, "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _display->print(tmp);

    _display->setCursor(0, 30);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);

#if defined(MECK_TWATCH)
    // Boot Other Slot button. First tap arms it (shown filled); a second tap
    // within SLOT_CONFIRM_MILLIS confirms.
    _display->setColor(DisplayDriver::LIGHT);
    if (_slot_armed) {
      _display->fillRect(SLOT_BTN_X, SLOT_BTN_Y, SLOT_BTN_W, SLOT_BTN_H);
      _display->setColor(DisplayDriver::DARK);
      _display->drawTextCentered(_display->width() / 2, SLOT_BTN_Y + 7, "Tap again: confirm");
    } else {
      _display->drawRect(SLOT_BTN_X, SLOT_BTN_Y, SLOT_BTN_W, SLOT_BTN_H);
      _display->drawTextCentered(_display->width() / 2, SLOT_BTN_Y + 7, "Boot Other Slot");
    }
    _home_shown = true;
#endif
  }
}

#if defined(MECK_TWATCH)
void UITask::showMessage(const char* line1, const char* line2) {
  if (!_display->isOn()) _display->turnOn();
  _display->startFrame();
  _display->setColor(DisplayDriver::LIGHT);
  _display->setTextSize(1);
  _display->drawTextCentered(_display->width() / 2, 50, line1);
  if (line2 && line2[0]) {
    _display->drawTextCentered(_display->width() / 2, 62, line2);
  }
  _display->endFrame();
  _home_shown = false;
  _next_refresh = 0;
}
#endif

void UITask::loop() {
#if defined(MECK_TWATCH)
  // The only key is the AXP2101 PWR key (PMUButton): a short press wakes the
  // display and restarts the auto-off timer.
  if (user_btn.check() == BUTTON_EVENT_CLICK) {
    if (!_display->isOn()) {
      _display->turnOn();
      _next_refresh = 0;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;
  }

  // An armed Boot Other Slot button lapses if the confirming tap never comes.
  if (_slot_armed && millis() > _slot_armed_until) {
    _slot_armed = false;
    _next_refresh = 0;
  }

  // Touch: only while the display is on.
  if (_display->isOn() && millis() >= _next_touch) {
    _next_touch = millis() + TOUCH_POLL_MILLIS;
    int tx, ty;
    if (display.getTouch(&tx, &ty)) {
      _touch_empty_polls = 0;
      if (!_touch_down) {   // a new tap
        _touch_down = true;
        _auto_off = millis() + AUTO_OFF_MILLIS;
        bool on_btn = _home_shown
                   && tx >= SLOT_BTN_X && tx < SLOT_BTN_X + SLOT_BTN_W
                   && ty >= SLOT_BTN_Y && ty < SLOT_BTN_Y + SLOT_BTN_H;
        if (on_btn && _slot_armed) {
          _slot_armed = false;
          _boot_other_slot_requested = true;   // picked up by main.cpp loop()
        } else if (on_btn) {
          _slot_armed = true;
          _slot_armed_until = millis() + SLOT_CONFIRM_MILLIS;
        } else {
          _slot_armed = false;   // a tap anywhere else cancels
        }
        _next_refresh = 0;
      }
    } else if (_touch_down) {
      if (++_touch_empty_polls >= TOUCH_LIFT_POLLS) {
        _touch_down = false;
        _touch_empty_polls = 0;
      }
    }
  }
#elif defined(PIN_USER_BTN)
  if (millis() >= _next_read) {
    int btnState = digitalRead(PIN_USER_BTN);
    if (btnState != _prevBtnState) {
      if (btnState == LOW) {
        if (!_display->isOn()) {
          _display->turnOn();
        }
        _auto_off = millis() + AUTO_OFF_MILLIS;
      }
      _prevBtnState = btnState;
    }
    _next_read = millis() + 200;
  }
#endif

  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

      _next_refresh = millis() + 10000;
#if defined(MECK_TWATCH)
      // Leave the boot screen for the home screen as soon as its time is up,
      // not at the next 10 second refresh.
      if (!_home_shown) _next_refresh = BOOT_SCREEN_MILLIS;
#endif
    }
    if (millis() > _auto_off) {
      _display->turnOff();
    }
  }
}
