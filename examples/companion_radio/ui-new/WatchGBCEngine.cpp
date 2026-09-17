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

// ---- State ------------------------------------------------------------------
static struct gb_s     *s_gb        = NULL;
static uint16_t        *s_gbfb      = NULL;             // GB_W*GB_H, RGB565
static uint16_t        *s_outfb[2]  = { NULL, NULL };   // OUT_W*OUT_H each, RGB565
static volatile int     s_ready     = 0;               // buffer index holding the last full frame
static int              s_write     = 0;               // buffer index the task writes into
static volatile bool    s_drew      = false;           // a drawn (not skipped) frame is ready to blit

static uint8_t         *s_rom       = NULL;
static size_t           s_rom_cap   = 0;               // arena capacity
static size_t           s_rom_size  = 0;               // bytes of the loaded ROM
static uint8_t         *s_cram      = NULL;            // 128 KB, PSRAM

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
  s_cram[addr] = val;      // no battery saves yet -- discarded on quit
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

// Nearest-neighbour scale 160x144 -> 240x216 into the write buffer via the
// precomputed source-index tables, then publish it for the UI thread.
static void blit_publish(void) {
  uint16_t *out = s_outfb[s_write];
  for (int oy = 0; oy < OUT_H; oy++) {
    const uint16_t *src = &s_gbfb[(size_t)s_srcy[oy] * GB_W];
    uint16_t *d = &out[(size_t)oy * OUT_W];
    for (int ox = 0; ox < OUT_W; ox++) d[ox] = src[s_srcx[ox]];
  }
  s_ready = s_write;
  s_write ^= 1;
}

// ---- Emulator task ----------------------------------------------------------
static void gbc_task(void *arg) {
  (void)arg;
  Serial.printf("[GBC] task start on core %d\n", (int)xPortGetCoreID());
  s_gb->direct.joypad = 0xFF;    // active-low: nothing pressed (no controls yet)

  int64_t next = esp_timer_get_time();
  unsigned frames_since_delay = 0;

  while (!s_stop) {
    gb_run_frame(s_gb);
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
