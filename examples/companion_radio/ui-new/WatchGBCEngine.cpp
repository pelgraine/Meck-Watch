// WatchGBCEngine -- see WatchGBCEngine.h. The Peanut-GB core is included and
// compiled only here.

#include "WatchGBCEngine.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Idle clock to return to on quit. The standalone envs define this as a build
// flag (40); fall back to the CPUPowerManager default otherwise.
#ifndef CPU_FREQ_IDLE
#define CPU_FREQ_IDLE 80
#endif

// ---- Core configuration (must precede the include) --------------------------
#define PEANUT_FULL_GBC_SUPPORT 1
#define ENABLE_LCD              1
#define ENABLE_SOUND            0
#include "peanut_gb.h"

// ---- Constants --------------------------------------------------------------
#define GB_W               160
#define GB_H               144
#define OUT_W              WGBC_OUT_W          // 240
#define OUT_H              WGBC_OUT_H          // 216
#define GBC_ROM_RESERVE    0x200000           // 2 MB arena on first launch
#define GBC_CRAM_SIZE      0x20000            // 128 KB, largest standard bank set
#define GBC_FRAME_US       16742              // 59.73 Hz
#define GBC_MAX_DEFICIT_US 250000             // behind by more than this = stall, resync
#define GBC_TASK_STACK     8192               // bytes, static, reserved at boot
#define GBC_TASK_PRIO      2
#define GBC_TASK_CORE      0
#define GBC_STOP_WAIT_MS   200                // max wait for the task to park on quit
#define GBC_PATH_MAX       128                // "/roms/" + filename + ".sav"

// ---- State ------------------------------------------------------------------
static struct gb_s     *s_gb        = NULL;
static uint16_t        *s_gbfb      = NULL;             // GB_W*GB_H, RGB565
static uint16_t        *s_outfb[2]  = { NULL, NULL };   // OUT_W*OUT_H each, RGB565
static volatile int     s_ready     = 0;               // buffer index holding the last full frame
static int              s_write     = 0;               // buffer index the task writes into
static volatile bool    s_drew      = false;           // a drawn (not skipped) frame is ready to blit
static volatile uint8_t s_touch_mask = 0;              // held controls, set by the UI thread each cycle

static uint8_t         *s_rom       = NULL;
static size_t           s_rom_cap   = 0;               // arena capacity
static size_t           s_rom_size  = 0;               // bytes of the loaded ROM
static uint8_t         *s_cram      = NULL;            // 128 KB, PSRAM
static size_t           s_save_size = 0;               // cart RAM this game really has, from the header
static char             s_save_path[GBC_PATH_MAX];     // the ROM path with the extension swapped to .sav
static volatile bool    s_cram_dirty   = false;        // the game has written cart RAM (emulator task)
static volatile bool    s_save_pending = false;        // save finished; the UI thread should write it out
static uint32_t         s_saved_sum    = 0;            // checksum of cart RAM as last written to .sav

static uint16_t         s_srcx[OUT_W];                 // nearest-neighbour output col -> source col
static uint16_t         s_srcy[OUT_H];                 // nearest-neighbour output row -> source row

static volatile bool    s_stop         = false;
static volatile bool    s_task_stopped = false;
static bool             s_running      = false;
static TaskHandle_t     s_task         = NULL;

static StackType_t      s_task_stack[GBC_TASK_STACK / sizeof(StackType_t)];  // .bss, internal RAM
static StaticTask_t     s_task_tcb;

// ---- Core callbacks ---------------------------------------------------------
static uint8_t rom_read(struct gb_s *gb, const uint_fast32_t addr) {
  (void)gb;
  return s_rom[addr];
}
static uint8_t cram_read(struct gb_s *gb, const uint_fast32_t addr) {
  (void)gb;
  return s_cram[addr];
}
static void cram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val) {
  (void)gb;
  s_cram[addr] = val;
  s_cram_dirty = true;     // a .sav write falls due once the game switches cart RAM off
}
static void gb_error_cb(struct gb_s *gb, const enum gb_error_e err, const uint16_t addr) {
  (void)gb;
  Serial.printf("[GBC] core error %d at 0x%04X -- stopping\n", (int)err, (unsigned)addr);
  s_stop = true;
}

// ---- Frame drawing ----------------------------------------------------------
// Peanut-GB (with the Meck patch) resolves every pixel to an RGB555 palette
// entry in fixPalette, for both DMG and CGB. Convert to RGB565 as we store the
// line: R and G shift up one (G widens to 6 bits, its top bit fills the new low
// G bit), B keeps its 5 bits. The last line marks the frame ready to blit.
static void draw_line(struct gb_s *gb, const uint8_t *pixels, const uint_fast8_t line) {
  uint16_t *dst = &s_gbfb[(size_t)line * GB_W];
  for (int x = 0; x < GB_W; x++) {
    const uint16_t v = gb->cgb.fixPalette[pixels[x]];
    dst[x] = (uint16_t)(((v & 0x7FE0) << 1) | ((v & 0x0200) >> 4) | (v & 0x1F));
  }
  if (line == GB_H - 1) s_drew = true;
}

// ---- On-screen control overlay ----------------------------------------------
// Controls follow the mockup: a translucent dark-navy fill (which stays visible
// on a light game background) with an opaque white outline (visible on a dark
// one), and a brighter fill on the held control. Labels are drawn on top by the
// screen.
static inline int iabs(int v) { return v < 0 ? -v : v; }

// Alpha-blend a foreground RGB565 over a background one. `a` is the foreground
// weight on a 0..32 scale.
static inline uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a) {
  const uint8_t ia = 32 - a;
  const uint16_t br = (bg >> 11) & 0x1F, bgc = (bg >> 5) & 0x3F, bb = bg & 0x1F;
  const uint16_t fr = (fg >> 11) & 0x1F, fgc = (fg >> 5) & 0x3F, fb = fg & 0x1F;
  return (uint16_t)((((br*ia + fr*a) >> 5) << 11) |
                    (((bgc*ia + fgc*a) >> 5) << 5) |
                     ((bb*ia + fb*a) >> 5));
}
// A disc: opaque white 2px ring, translucent fill inside.
static void draw_disc(uint16_t *out, int cx, int cy, int r, uint16_t fill, uint8_t a) {
  const int r2 = r * r, ri2 = (r - 2) * (r - 2);
  for (int dy = -r; dy <= r; dy++) {
    const int yy = cy + dy; if (yy < 0 || yy >= OUT_H) continue;
    for (int dx = -r; dx <= r; dx++) {
      const int xx = cx + dx; if (xx < 0 || xx >= OUT_W) continue;
      const int d2 = dx*dx + dy*dy; if (d2 > r2) continue;
      uint16_t *p = &out[(size_t)yy * OUT_W + xx];
      *p = (d2 > ri2) ? 0xFFFF : blend565(*p, fill, a);
    }
  }
}
// A pill: opaque white 2px border, translucent fill inside. (x,y) = top-left.
static void draw_pill(uint16_t *out, int x, int y, int w, int h, uint16_t fill, uint8_t a) {
  for (int j = 0; j < h; j++) {
    const int yy = y + j; if (yy < 0 || yy >= OUT_H) continue;
    for (int i = 0; i < w; i++) {
      const int xx = x + i; if (xx < 0 || xx >= OUT_W) continue;
      uint16_t *p = &out[(size_t)yy * OUT_W + xx];
      *p = (i < 2 || i >= w - 2 || j < 2 || j >= h - 2) ? 0xFFFF : blend565(*p, fill, a);
    }
  }
}
static inline bool in_plus(int dx, int dy, int arm, int th) {
  return (iabs(dx) <= th/2 && iabs(dy) <= arm) || (iabs(dy) <= th/2 && iabs(dx) <= arm);
}
// A d-pad plus: opaque white 2px outline, translucent fill, pressed arm brighter.
static void draw_dpad(uint16_t *out, int cx, int cy, int arm, int th, int dead,
                      uint8_t mask, uint16_t fill, uint16_t press, uint8_t a) {
  for (int dy = -arm; dy <= arm; dy++) {
    const int yy = cy + dy; if (yy < 0 || yy >= OUT_H) continue;
    for (int dx = -arm; dx <= arm; dx++) {
      const int xx = cx + dx; if (xx < 0 || xx >= OUT_W) continue;
      if (!in_plus(dx, dy, arm, th)) continue;
      uint16_t *p = &out[(size_t)yy * OUT_W + xx];
      if (!in_plus(dx-2, dy, arm, th) || !in_plus(dx+2, dy, arm, th) ||
          !in_plus(dx, dy-2, arm, th) || !in_plus(dx, dy+2, arm, th)) { *p = 0xFFFF; continue; }
      uint16_t c = fill;
      if      ((mask & WGBC_UP)    && iabs(dx) <= th/2 && dy < -dead) c = press;
      else if ((mask & WGBC_DOWN)  && iabs(dx) <= th/2 && dy >  dead) c = press;
      else if ((mask & WGBC_LEFT)  && iabs(dy) <= th/2 && dx < -dead) c = press;
      else if ((mask & WGBC_RIGHT) && iabs(dy) <= th/2 && dx >  dead) c = press;
      *p = blend565(*p, c, a);
    }
  }
}
// Dark controls with white outlines; the held one brightens. Labels are drawn
// on top by the screen (this buffer has no font).
static void composite_overlay(uint16_t *out, uint8_t mask) {
  const uint16_t fill  = 0x214B;   // dark navy
  const uint16_t press = 0x4BDB;   // brighter blue when pressed
  const uint8_t  a = 10;           // fill opacity on the 0..32 scale -- light
                                   // enough to read the game through; the
                                   // outlines stay solid for definition

  draw_dpad(out, WGBC_DPAD_CX, WGBC_DPAD_CY, WGBC_DPAD_HALF, WGBC_DPAD_HALF*2/3,
            WGBC_DPAD_DEAD, mask, fill, press, a);
  draw_disc(out, WGBC_A_CX, WGBC_A_CY, WGBC_BTN_R, (mask & WGBC_A) ? press : fill, a);
  draw_disc(out, WGBC_B_CX, WGBC_B_CY, WGBC_BTN_R, (mask & WGBC_B) ? press : fill, a);
  draw_pill(out, WGBC_SEL_CX - WGBC_SEL_W/2, WGBC_SEL_CY - WGBC_PILL_H/2,
            WGBC_SEL_W, WGBC_PILL_H, (mask & WGBC_SELECT) ? press : fill, a);
  draw_pill(out, WGBC_START_CX - WGBC_START_W/2, WGBC_START_CY - WGBC_PILL_H/2,
            WGBC_START_W, WGBC_PILL_H, (mask & WGBC_START) ? press : fill, a);
  draw_disc(out, WGBC_BACK_CX, WGBC_BACK_CY, WGBC_BACK_R, fill, a);
}

// Nearest-neighbour scale 160x144 -> 240x216 into the write buffer via the
// precomputed source-index tables, composite the control overlay, then publish
// it for the UI thread.
static void blit_publish(void) {
  uint16_t *out = s_outfb[s_write];
  for (int oy = 0; oy < OUT_H; oy++) {
    const uint16_t *src = &s_gbfb[(size_t)s_srcy[oy] * GB_W];
    uint16_t *d = &out[(size_t)oy * OUT_W];
    for (int ox = 0; ox < OUT_W; ox++) d[ox] = src[s_srcx[ox]];
  }
  composite_overlay(out, s_touch_mask);
  s_ready = s_write;
  s_write ^= 1;
}

// ---- Emulator task ----------------------------------------------------------
static void gbc_task(void *arg) {
  (void)arg;
  Serial.printf("[GBC] task start on core %d\n", (int)xPortGetCoreID());

  int64_t next = esp_timer_get_time();
  unsigned frames_since_delay = 0;

  while (!s_stop) {
    s_gb->direct.joypad = (uint8_t)~s_touch_mask;   // active-low; set from touch
    gb_run_frame(s_gb);
    // A game enables cart RAM, writes its save, then switches it off again.
    // That switch-off is the game reporting the save is complete. Flag it for
    // the UI thread: the flash write must not happen on this task.
    if (s_cram_dirty && !s_gb->enable_cart_ram) { s_cram_dirty = false; s_save_pending = true; }
    if (s_drew) { blit_publish(); s_drew = false; }

    // Pace to the accumulated deadline. If we fall behind, keep the deficit and
    // run frames back-to-back until it is paid off; only a real stall resyncs.
    // Give the idle task a slice at least every 30 frames or the 5 s task
    // watchdog reboots the board.
    next += GBC_FRAME_US;
    const int64_t now = esp_timer_get_time();
    if (next > now) {
      const int64_t rem_ms = (next - now) / 1000;
      if (rem_ms >= 1) {
        vTaskDelay(pdMS_TO_TICKS(rem_ms));
        frames_since_delay = 0;
        continue;
      }
    } else if (now - next > GBC_MAX_DEFICIT_US) {
      next = now;
    }
    if (++frames_since_delay >= 30) { vTaskDelay(1); frames_since_delay = 0; }
  }

  Serial.printf("[GBC] task stop: stack high-water %u bytes free of %u\n",
                (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                (unsigned)GBC_TASK_STACK);
  s_task_stopped = true;
  vTaskDelete(NULL);
}

// ---- Buffers (allocated once, kept between launches) ------------------------
static bool ensure_buffers(void) {
  if (!s_gb)       s_gb       = (struct gb_s*)heap_caps_malloc(sizeof(struct gb_s), MALLOC_CAP_SPIRAM);
  if (!s_gbfb)     s_gbfb     = (uint16_t*)heap_caps_malloc((size_t)GB_W * GB_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  if (!s_outfb[0]) s_outfb[0] = (uint16_t*)heap_caps_malloc((size_t)OUT_W * OUT_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  if (!s_outfb[1]) s_outfb[1] = (uint16_t*)heap_caps_malloc((size_t)OUT_W * OUT_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  if (!s_cram)     s_cram     = (uint8_t*)heap_caps_malloc(GBC_CRAM_SIZE, MALLOC_CAP_SPIRAM);
  if (!s_gb || !s_gbfb || !s_outfb[0] || !s_outfb[1] || !s_cram) {
    Serial.printf("[GBC] buffer alloc failed (largest free PSRAM block %u)\n",
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    return false;
  }
  // Nearest-neighbour source-index tables (output position * 2/3, clamped).
  for (int ox = 0; ox < OUT_W; ox++) { int s = (ox * GB_W) / OUT_W; s_srcx[ox] = (uint16_t)((s < GB_W) ? s : GB_W - 1); }
  for (int oy = 0; oy < OUT_H; oy++) { int s = (oy * GB_H) / OUT_H; s_srcy[oy] = (uint16_t)((s < GB_H) ? s : GB_H - 1); }
  return true;
}

// ---- Public API -------------------------------------------------------------
// ---- Battery saves ----------------------------------------------------------
// The .sav sits beside the ROM and holds the cartridge's battery-backed RAM,
// at the size the ROM header declares. It is written when the game finishes a
// save and once more on quit -- never on a timer.
static uint32_t cram_sum(void) {
  uint32_t s = 0;
  for (size_t i = 0; i < s_save_size; i++) s = s * 31u + s_cram[i];
  return s;
}
// "/roms/Game.gbc" -> "/roms/Game.sav"
static void build_save_path(const char *romPath) {
  snprintf(s_save_path, sizeof(s_save_path), "%s", romPath);
  char *dot = strrchr(s_save_path, '.');
  const char *slash = strrchr(s_save_path, '/');
  if (dot && (!slash || dot > slash)) *dot = 0;
  strncat(s_save_path, ".sav", sizeof(s_save_path) - strlen(s_save_path) - 1);
}
// Read the .sav into cart RAM. Before the task starts, so no lock is needed.
static void load_save(void) {
  if (s_save_size) {
    File f = LittleFS.open(s_save_path, "r");
    if (f) {
      const size_t avail = (size_t)f.size();
      const size_t want  = (avail < s_save_size) ? avail : s_save_size;
      const size_t got   = f.read(s_cram, want);
      f.close();
      Serial.printf("[GBC] save loaded: %u bytes from %s\n", (unsigned)got, s_save_path);
    } else {
      Serial.printf("[GBC] no save yet at %s\n", s_save_path);
    }
  }
  s_cram_dirty   = false;      // the load is not the game writing
  s_save_pending = false;
  s_saved_sum    = cram_sum(); // cart RAM now matches what is on disk
}
// Write cart RAM out. UI thread only -- this blocks on flash.
static void write_save(void) {
  if (!s_save_size || !s_cram) return;
  const uint32_t sum = cram_sum();
  if (sum == s_saved_sum) return;     // identical to the file already there
  File f = LittleFS.open(s_save_path, "w");
  if (!f) { Serial.printf("[GBC] cannot write %s\n", s_save_path); return; }
  const size_t got = f.write(s_cram, s_save_size);
  f.close();
  if (got == s_save_size) {
    s_saved_sum = sum;
    Serial.printf("[GBC] saved %u bytes to %s\n", (unsigned)got, s_save_path);
  } else {
    Serial.printf("[GBC] short save write (%u/%u)\n", (unsigned)got, (unsigned)s_save_size);
  }
}

bool wgbc_start(const char* romPath) {
  if (s_running) return false;
  if (!ensure_buffers()) return false;

  File f = LittleFS.open(romPath, "r");
  if (!f) { Serial.printf("[GBC] cannot open %s\n", romPath); return false; }
  const size_t sz = (size_t)f.size();
  if (sz < 0x8000) { f.close(); Serial.println("[GBC] file too small to be a ROM"); return false; }

  if (s_rom && s_rom_cap < sz) { heap_caps_free(s_rom); s_rom = NULL; s_rom_cap = 0; }
  if (!s_rom) {
    size_t want = (sz > GBC_ROM_RESERVE) ? sz : GBC_ROM_RESERVE;
    s_rom = (uint8_t*)heap_caps_malloc(want, MALLOC_CAP_SPIRAM);
    if (!s_rom && want > sz) { want = sz; s_rom = (uint8_t*)heap_caps_malloc(want, MALLOC_CAP_SPIRAM); }
    if (!s_rom) {
      f.close();
      Serial.printf("[GBC] ROM alloc failed (%u bytes; largest free PSRAM block %u)\n",
                    (unsigned)sz, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
      return false;
    }
    s_rom_cap = want;
    Serial.printf("[GBC] ROM arena reserved: %u bytes\n", (unsigned)want);
  }
  const size_t got = f.read(s_rom, sz);
  f.close();
  if (got != sz) { Serial.printf("[GBC] short read (%u/%u)\n", (unsigned)got, (unsigned)sz); return false; }
  s_rom_size = sz;

  memset(s_cram, 0, GBC_CRAM_SIZE);
  const enum gb_init_error_e e = gb_init(s_gb, rom_read, cram_read, cram_write, gb_error_cb, NULL);
  if (e != GB_INIT_NO_ERROR) { Serial.printf("[GBC] gb_init failed: %d\n", (int)e); return false; }
  gb_init_lcd(s_gb, draw_line);
  s_gb->direct.frame_skip = 1;

  // Header is parsed now, so the real cart RAM size is known.
  s_save_size = (size_t)gb_get_save_size(s_gb);
  if (s_save_size > GBC_CRAM_SIZE) s_save_size = GBC_CRAM_SIZE;
  build_save_path(romPath);
  load_save();

  memset(s_gbfb, 0, (size_t)GB_W * GB_H * sizeof(uint16_t));
  memset(s_outfb[0], 0, (size_t)OUT_W * OUT_H * sizeof(uint16_t));
  memset(s_outfb[1], 0, (size_t)OUT_W * OUT_H * sizeof(uint16_t));
  s_write = 0; s_ready = 0; s_drew = false;
  Serial.printf("[GBC] start: cgbMode=%d mbc=%d rom=%u bytes\n",
                (int)s_gb->cgb.cgbMode, (int)s_gb->mbc, (unsigned)s_rom_size);

  setCpuFrequencyMhz(240);   // the core needs 240 MHz throughout

  s_stop = false;
  s_task_stopped = false;
  s_task = xTaskCreateStaticPinnedToCore(gbc_task, "meck_gbc", GBC_TASK_STACK, NULL,
                                         GBC_TASK_PRIO, s_task_stack, &s_task_tcb, GBC_TASK_CORE);
  if (s_task == NULL) {
    Serial.println("[GBC] task create failed");
    setCpuFrequencyMhz(CPU_FREQ_IDLE);
    return false;
  }
  s_running = true;
  return true;
}

void wgbc_stop() {
  if (!s_running) return;
  s_stop = true;
  const uint32_t t0 = millis();
  while (!s_task_stopped && (uint32_t)(millis() - t0) < GBC_STOP_WAIT_MS) delay(1);
  s_task = NULL;
  s_running = false;
  write_save();              // last write, so quitting never loses the save
  setCpuFrequencyMhz(CPU_FREQ_IDLE);
}

bool wgbc_running() { return s_running; }

const uint16_t* wgbc_frame() {
  if (!s_outfb[0]) return NULL;
  return s_outfb[s_ready];
}

void wgbc_hold_cpu() {
  if (getCpuFrequencyMhz() < 240) setCpuFrequencyMhz(240);
}

void wgbc_set_joypad(uint8_t mask) { s_touch_mask = mask; }

void wgbc_poll_save() {
  if (!s_save_pending) return;
  s_save_pending = false;
  write_save();
}

uint8_t wgbc_hittest(int gx, int gy) {
  uint8_t mask = 0;
  { const int dx = gx - WGBC_DPAD_CX, dy = gy - WGBC_DPAD_CY;
    if (dx >= -WGBC_DPAD_HALF && dx <= WGBC_DPAD_HALF && dy >= -WGBC_DPAD_HALF && dy <= WGBC_DPAD_HALF) {
      if (dx >  WGBC_DPAD_DEAD) mask |= WGBC_RIGHT;
      if (dx < -WGBC_DPAD_DEAD) mask |= WGBC_LEFT;
      if (dy < -WGBC_DPAD_DEAD) mask |= WGBC_UP;
      if (dy >  WGBC_DPAD_DEAD) mask |= WGBC_DOWN;
    } }
  { const int r2 = WGBC_BTN_R * WGBC_BTN_R;
    int dx = gx - WGBC_A_CX, dy = gy - WGBC_A_CY; if (dx*dx + dy*dy <= r2) mask |= WGBC_A;
    dx = gx - WGBC_B_CX; dy = gy - WGBC_B_CY;     if (dx*dx + dy*dy <= r2) mask |= WGBC_B; }
  { const int hh = WGBC_PILL_H/2 + 6;
    if (gx >= WGBC_SEL_CX - WGBC_SEL_W/2 - 6 && gx <= WGBC_SEL_CX + WGBC_SEL_W/2 + 6 &&
        gy >= WGBC_SEL_CY - hh && gy <= WGBC_SEL_CY + hh) mask |= WGBC_SELECT;
    if (gx >= WGBC_START_CX - WGBC_START_W/2 - 6 && gx <= WGBC_START_CX + WGBC_START_W/2 + 6 &&
        gy >= WGBC_START_CY - hh && gy <= WGBC_START_CY + hh) mask |= WGBC_START; }
  return mask;
}

bool wgbc_hit_exit(int gx, int gy) {
  const int dx = gx - WGBC_BACK_CX, dy = gy - WGBC_BACK_CY, r = WGBC_BACK_R + 6;
  return dx*dx + dy*dy <= r*r;
}