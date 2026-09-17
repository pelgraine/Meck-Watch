#pragma once

#include <stdint.h>
#include <stddef.h>

// =============================================================================
// WatchGBCEngine -- Game Boy / Game Boy Color emulator engine for the Meck
// watch (stage 2).
//
// Runs the Peanut-GB core in its own FreeRTOS task (pinned to core 0, static
// 8 KB stack) and produces a scaled RGB565 frame that the UI thread pushes
// straight to the panel. All large buffers live in PSRAM. No input, no sound,
// and no battery saves yet -- a game plays its intro/attract only.
//
// The core is included and compiled only in WatchGBCEngine.cpp. Everything
// here is a plain declaration, safe to include from the screen header.
// =============================================================================

// Output picture size: 160x144 scaled 1.5x, nearest neighbour.
#define WGBC_OUT_W  240
#define WGBC_OUT_H  216

// Start emulating the ROM at romPath (a LittleFS path on the "/maps"
// partition, e.g. "/roms/game.gbc"). Allocates PSRAM buffers on first use,
// loads the ROM, starts the task, and raises the CPU to 240 MHz. Returns false
// on any failure (buffer alloc, ROM open/read, unsupported ROM, task create);
// on failure nothing is left running and the clock is not raised.
bool wgbc_start(const char* romPath);

// Stop the running game: ask the task to park, wait for it (bounded), and drop
// the CPU back to idle. Buffers are kept for the next launch.
void wgbc_stop();

// True while the emulator task is running.
bool wgbc_running();

// Pointer to the most recently completed frame (WGBC_OUT_W x WGBC_OUT_H,
// RGB565), or NULL if no frame has been produced yet. Safe to read from the UI
// thread: the task double-buffers and only publishes complete frames.
const uint16_t* wgbc_frame();

// Re-assert the 240 MHz clock if something lowered it (a touch event can call
// the power manager's boost, which caps at 80 MHz). Call once per UI cycle
// while a game runs.
void wgbc_hold_cpu();
