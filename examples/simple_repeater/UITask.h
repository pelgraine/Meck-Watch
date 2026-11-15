#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <helpers/CommonCLI.h>

class UITask {
  DisplayDriver* _display;
  unsigned long _next_read, _next_refresh, _auto_off;
  int _prevBtnState;
  NodePrefs* _node_prefs;
  char _version_info[48];
#if defined(MECK_TWATCH)
  char _build_date[24];
  unsigned long _next_touch;       // next touch poll
  bool _touch_down;                // a touch is in progress
  uint8_t _touch_empty_polls;      // empty polls in a row while a touch is in progress
  bool _home_shown;                // the home screen (with the button) is what is on the display
  bool _slot_armed;                // first tap on Boot Other Slot done, waiting for the confirming tap
  unsigned long _slot_armed_until; // the armed state lapses at this time
  bool _boot_other_slot_requested; // set by the confirming tap; main.cpp flips the boot slot and restarts
#endif

  void renderCurrScreen();
public:
  UITask(DisplayDriver& display) : _display(&display) { _next_read = _next_refresh = 0; }
  void begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version);

#if defined(MECK_TWATCH)
  bool isBootOtherSlotRequested() const { return _boot_other_slot_requested; }
  void clearBootOtherSlotRequest() { _boot_other_slot_requested = false; }
  // Draw up to two centred lines straight away (the caller is about to block or restart).
  void showMessage(const char* line1, const char* line2);
#endif

  void loop();
};
