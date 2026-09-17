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

// ---- On-screen controls (stage 3) -------------------------------------------
// Layout in game-buffer pixels: (0,0) is the top-left of the 240x216 picture.
// Shared by the engine (which draws the overlay and hit-tests) and the screen
// (which labels the buttons and converts touch coordinates). WGBC_GAME_Y is the
// panel row the picture is pushed to, so panel_y = game_y + WGBC_GAME_Y.
// Positions and sizes follow the 240x240 control mockup: back top-right, the
// D-pad lower-left, A/B right, sel/start along the bottom.
#define WGBC_GAME_Y     12
#define WGBC_DPAD_CX    52
#define WGBC_DPAD_CY    98
#define WGBC_DPAD_HALF  43
#define WGBC_DPAD_DEAD  14
#define WGBC_A_CX       204
#define WGBC_A_CY       82
#define WGBC_B_CX       160
#define WGBC_B_CY       118
#define WGBC_BTN_R      24
#define WGBC_SEL_CX     32
#define WGBC_SEL_CY     19
#define WGBC_SEL_W      46
#define WGBC_START_CX   98
#define WGBC_START_CY   19
#define WGBC_START_W    56
#define WGBC_PILL_H     20
#define WGBC_BACK_CX    216
#define WGBC_BACK_CY    19
#define WGBC_BACK_R     18

// Joypad mask bits: 0=A 1=B 2=Select 3=Start 4=Right 5=Left 6=Up 7=Down.
#define WGBC_A      0x01
#define WGBC_B      0x02
#define WGBC_SELECT 0x04
#define WGBC_START  0x08
#define WGBC_RIGHT  0x10
#define WGBC_LEFT   0x20
#define WGBC_UP     0x40
#define WGBC_DOWN   0x80

// Set the joypad from a touch hit-test mask; the task applies it each frame.
void wgbc_set_joypad(uint8_t mask);

// Write the .sav out if the game has just finished saving. UI thread only --
// the write blocks on flash. Call once per render while a game is running.
// Saves also happen once on quit, inside wgbc_stop(). There is no save timer.
void wgbc_poll_save();

// Hit-test a point in game-buffer space: wgbc_hittest returns a joypad mask,
// wgbc_hit_exit is true over the on-screen EXIT button.
uint8_t wgbc_hittest(int gx, int gy);
bool    wgbc_hit_exit(int gx, int gy);