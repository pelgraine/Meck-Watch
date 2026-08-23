#pragma once

// Watch File Manager: a browser-based file explorer for the internal
// filesystems, in the same manner as the upstream Meck OTA Tools' SD File
// Manager -- the watch starts a WiFi access point and serves a page at
// http://192.168.4.1/ where files on the LittleFS ("maps") and SPIFFS
// (DataStore) partitions can be browsed folder-by-folder, downloaded,
// uploaded and deleted. The AP is named Meck-Files-XXXX and a DNS captive
// portal opens the page automatically on most phones.
// There is no firmware-update path here: the watch partition table has a
// single app slot (see meck_twatch_16mb.csv).
//
// Toggled from Settings -> File Manager; pumped from main.cpp loop().
// Auto-stops after five minutes with no phone connected to the hotspot.

#if defined(MECK_TWATCH)

bool        wfmActive();
bool        wfmTakeIdleStopped();  // true once, right after an idle-timeout auto-stop
const char* wfmSSID();     // AP name, valid while active
void        wfmStart();
void        wfmStop();
void        wfmLoop();     // no-op when inactive

#endif