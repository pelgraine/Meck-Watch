#pragma once

// =============================================================================
// WatchGBCScreen -- Game Boy emulator for the Meck watch.
//
// Browser mode: lists .gb/.gbc ROMs in /roms on the "/maps" LittleFS partition
// (the folder is created on first open). Tapping a filename starts the game.
//
// Game mode: WatchGBCEngine runs the Peanut-GB core in its own task at 240 MHz
// and produces a scaled RGB565 frame; render() pushes it straight to the panel
// (the render loop skips its usual sprite push while a game runs, so the two
// do not fight). There are no on-screen controls yet, so a game plays its
// intro/attract only.
//
// The back gesture on the watch is a long press, delivered as 'q': from a game
// it stops the emulator and returns to the ROM list; from the ROM list it sets
// wantsExit() so main.cpp returns to the games menu. A top-strip tap goes home,
// handled generically in main.cpp.
// =============================================================================

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>
#if defined(MECK_TWATCH)
#include <helpers/ui/LGFXDisplay.h>
#endif
#include "WatchGBCEngine.h"

// Forward declaration
class UITask;

class WatchGBCScreen : public UIScreen {
private:
  static const int GBC_MAX_ROMS = 32;
  static const int GBC_NAME_MAX = 96;

  UITask* _task;

  enum Mode { BROWSER, GAME };
  Mode _mode;
  bool _wantsExit;
  bool _gameFirstFrame;

  char _romNames[GBC_MAX_ROMS][GBC_NAME_MAX];
  int  _romCount;
  int  _cursor;      // highlighted row in the ROM list
  int  _selected;    // opened ROM index
  int  _startIdx;    // first visible row (set in render, read by selectRowAtVY)
  int  _maxVisible;  // visible row count (set in render, read by selectRowAtVY)
  char _status[28];  // transient message shown in the browser (e.g. a load error)

  void scanRoms() {
    _romCount = 0;
    if (!LittleFS.exists("/roms")) {
      LittleFS.mkdir("/roms");
    }
    File root = LittleFS.open("/roms");
    if (!root || !root.isDirectory()) return;

    File f = root.openNextFile();
    while (f && _romCount < GBC_MAX_ROMS) {
      String name = String(f.name());
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);

      if (!f.isDirectory() && !name.startsWith(".") &&
          (name.endsWith(".gb")  || name.endsWith(".GB") ||
           name.endsWith(".gbc") || name.endsWith(".GBC"))) {
        strncpy(_romNames[_romCount], name.c_str(), GBC_NAME_MAX - 1);
        _romNames[_romCount][GBC_NAME_MAX - 1] = 0;
        _romCount++;
      }
      f = root.openNextFile();
    }
    root.close();
  }

  void openSelected() {
    _selected = _cursor;
    char path[GBC_NAME_MAX + 8];
    snprintf(path, sizeof(path), "/roms/%s", _romNames[_selected]);
    if (wgbc_start(path)) {
      _mode = GAME;
      _gameFirstFrame = true;
      _status[0] = 0;
    } else {
      snprintf(_status, sizeof(_status), "Load failed");
    }
  }

  int renderGame(DisplayDriver& display) {
#if defined(MECK_TWATCH)
    LGFXDisplay& d = (LGFXDisplay&)display;
    if (_gameFirstFrame) { d.fillPanel565(0x0000); _gameFirstFrame = false; }
    const uint16_t* fb = wgbc_frame();
    if (fb) d.pushImageToPanel565(0, (240 - WGBC_OUT_H) / 2, WGBC_OUT_W, WGBC_OUT_H, fb);
    wgbc_hold_cpu();
#endif
    return 33;   // ~30 Hz, matching the drawn frame rate
  }

  int renderBrowser(DisplayDriver& display) {
    display.startFrame();
    display.setTextSize(1);

    // Header
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(2, 2);
    display.print("Game Boy");
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 12, display.width(), 1);

    if (_romCount == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(6, 40);
      display.print("No ROMs in /roms");
      display.setTextSize(0);
      display.setCursor(6, 58);
      display.print("Add .gb or .gbc files");
      display.setTextSize(1);
      if (_status[0]) {
        display.setTextSize(0);
        display.drawTextCentered(display.width() / 2, display.height() - 8, _status);
        display.setTextSize(1);
      }
      return 1000;
    }

    const int listTop = 18, lineH = 16;
    int footerY = display.height() - 14;
    _maxVisible = (footerY - listTop) / lineH;
    if (_maxVisible < 1) _maxVisible = 1;
    _startIdx = _cursor - _maxVisible / 2;
    if (_startIdx > _romCount - _maxVisible) _startIdx = _romCount - _maxVisible;
    if (_startIdx < 0) _startIdx = 0;

    int y = listTop;
    for (int i = _startIdx; i < _romCount && i < _startIdx + _maxVisible; i++) {
      bool selected = (i == _cursor);
      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y - 1, display.width(), lineH);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }
      display.setCursor(6, y + 2);
      display.print(_romNames[i]);
      y += lineH;
    }

    // Footer (or a transient status message if one is set)
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(0);
    display.drawTextCentered(display.width() / 2, display.height() - 8,
                             _status[0] ? _status : "Tap to open");
    display.setTextSize(1);
    return 1000;
  }

public:
  WatchGBCScreen(UITask* task)
    : _task(task), _mode(BROWSER), _wantsExit(false), _gameFirstFrame(false),
      _romCount(0), _cursor(0), _selected(-1),
      _startIdx(0), _maxVisible(1) { _status[0] = 0; }

  bool wantsExit() const { return _wantsExit; }
  bool isInFileList() const { return _mode == BROWSER; }
  bool isInGame() const { return _mode == GAME; }

  void enter() {
    _wantsExit = false;
    _mode = BROWSER;
    _cursor = 0;
    _selected = -1;
    _status[0] = 0;
    scanRoms();
  }

  // ------- Input -------
  bool handleInput(char c) override {
    if (_mode == BROWSER) {
      switch (c) {
        case 'w': case 'W':
          if (_cursor > 0) _cursor--;
          _status[0] = 0;
          return true;
        case 's': case 'S':
          if (_cursor < _romCount - 1) _cursor++;
          _status[0] = 0;
          return true;
        case KEY_ENTER:
          if (_romCount > 0) openSelected();
          return true;
        case 'q':
          _wantsExit = true;
          return true;
        default:
          return false;
      }
    } else {  // GAME
      if (c == 'q') { wgbc_stop(); _mode = BROWSER; scanRoms(); return true; }
      return false;
    }
  }

  // ------- Touch: tap-to-select in the ROM list -------
  int selectRowAtVY(int vy) {
    if (_mode != BROWSER || _romCount == 0) return 0;
    const int listTop = 18, lineH = 16;
    if (vy < listTop) return 0;
    int row = _startIdx + (vy - listTop) / lineH;
    if (row < 0 || row >= _romCount || row >= _startIdx + _maxVisible) return 0;
    if (row == _cursor) return 2;   // tapped current selection -- open
    _cursor = row;
    _status[0] = 0;
    return 1;                        // moved cursor
  }

  // ------- Render -------
  int render(DisplayDriver& display) override {
    if (_mode == GAME) return renderGame(display);
    return renderBrowser(display);
  }
};