// =============================================================================
// Meck Watch audio test -- LilyGo T-Watch S3 (SX1280 build family)
//
// A throwaway hardware test, built as its own PlatformIO environment
// (meck_twatch_s3_1280_audiotest). It shares no code with Meck Watch and
// never touches the radio. Its only job is to answer, on real hardware:
//
//   1. Does the PDM mic record? (level, peak, DC offset)
//   2. Can a recording be saved to and read back from the watch's flash?
//      (LittleFS 'maps' partition, folder /voicetest, mounted WITHOUT
//      format-on-fail so existing books / alarm settings are never wiped)
//   3. Does the MAX98357A speaker play it? (DLDO1 rail on, I2S_NUM_1)
//   4. Can mic (I2S_NUM_0) and speaker (I2S_NUM_1) run at the same time?
//   5. What do Codec2 versions sound like on the watch speaker, and how long
//      do they take to encode and decode at 80 MHz? (see "Codec2 comparison")
//
// Use: open the serial monitor, press the side PWR key briefly, wait for the
// countdown, speak for 5 seconds, then listen. Results are printed and also
// saved to /voicetest/results.txt, which is printed again at every boot.
//
// The screen stays dark: this build switches the display, LoRa and haptic
// rails off and only powers the speaker rail.
//
// Mic settings follow LilyGo's own T-Watch S3 library (TTGO_TWatch_Library,
// branch t-watch-s3, LilyGoLib::initMicrophone). Speaker on I2S port 1 follows
// LilyGo's factory firmware (AudioOutputI2S(1, EXTERNAL_I2S)). Recording,
// normalising and Codec2 framing follow Meck's VoiceMessageScreen.h.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <FS.h>
#include <LittleFS.h>
#include <driver/i2s.h>
#include <esp_timer.h>
#include "XPowersLib.h"
#include <codec2.h>

// ---- Board pins (T-Watch S3 schematic / LilyGo utilities.h) ----------------
#define TEST_I2C_SDA        10
#define TEST_I2C_SCL        11
#define TEST_PMU_ADDR       0x34

#define TEST_MIC_CLOCK      44   // PDM clock
#define TEST_MIC_DATA       47   // PDM data
#define TEST_SPK_BCLK       48   // MAX98357A BCLK
#define TEST_SPK_WS         15   // MAX98357A LRCLK
#define TEST_SPK_DOUT       46   // MAX98357A DIN

#define TEST_MIC_PORT       I2S_NUM_0   // PDM RX is only available on port 0
#define TEST_SPK_PORT       I2S_NUM_1

// ---- Audio settings ---------------------------------------------------------
#define TEST_SAMPLE_RATE    16000       // LilyGo: PDM mic max 16 kHz
#define TEST_RECORD_SECONDS 5
#define TEST_REC_SAMPLES    (TEST_SAMPLE_RATE * TEST_RECORD_SECONDS)

#define TEST_CPU_NORMAL     80          // what the SX1280 watch builds run at
// Earlier runs of this test measured Codec2 1200 encoding at 160 and 240 MHz,
// and found the watch reset (TG1WDT_SYS_RST) right after switching
// 80 -> 40 MHz. This version stays at 80 MHz throughout.

#define TEST_DIR            "/voicetest"
#define TEST_WAV_PATH       "/voicetest/rec.wav"
#define TEST_RESULTS_PATH   "/voicetest/results.txt"

// ---- State ------------------------------------------------------------------
static XPowersAXP2101* pmu = NULL;
static bool fsReady = false;
static int  runCount = 0;

static int16_t* recBuf  = NULL;   // 16 kHz recording (PSRAM)
static int16_t* playBuf = NULL;   // 16 kHz decoded Codec2 for playback (PSRAM)
static uint8_t* c2Buf   = NULL;   // encoded Codec2 bytes (PSRAM)
static uint32_t recSamples = 0;

static String results;            // summary for this run

static void note(const char* fmt, ...) {
  char line[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  Serial.println(line);
  results += line;
  results += "\n";
}

// =============================================================================
// Power
// =============================================================================
static bool powerInit() {
  pmu = new XPowersAXP2101(Wire, TEST_I2C_SDA, TEST_I2C_SCL, TEST_PMU_ADDR);
  if (!pmu->init()) {
    Serial.println("PMU: AXP2101 not found");
    delete pmu;
    pmu = NULL;
    return false;
  }

  // Only the speaker rail is wanted. Display, touch, LoRa and haptic are
  // switched off explicitly because the AXP2101 keeps whatever state the
  // previous firmware left it in.
  pmu->disableALDO2();   // display backlight
  pmu->disableALDO3();   // display + touch
  pmu->disableALDO4();   // LoRa radio
  pmu->disableBLDO2();   // DRV2605 haptic
  // DLDO1 = MAX98357A VDD (schematic sheet 6, SPK_VDD). LilyGo's own library
  // enables it without setting a voltage, so this does the same and reports
  // what voltage it is at.
  pmu->enableDLDO1();

  // Short press of the side PWR key starts a test run.
  pmu->disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  pmu->clearIrqStatus();
  pmu->enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);

  Serial.printf("PMU: DLDO1 (speaker) %s, %u mV\n",
                pmu->isEnableDLDO1() ? "ON" : "OFF", pmu->getDLDO1Voltage());
  return true;
}

static bool pwrKeyClicked() {
  if (!pmu) return false;
  pmu->getIrqStatus();
  bool clicked = pmu->isPekeyShortPressIrq();
  pmu->clearIrqStatus();
  return clicked;
}

// =============================================================================
// Storage
// =============================================================================
static void writeWavHeader(File& f, uint32_t dataBytes) {
  uint32_t byteRate = TEST_SAMPLE_RATE * 2;
  uint8_t h[44] = {
    'R','I','F','F', 0,0,0,0, 'W','A','V','E',
    'f','m','t',' ', 16,0,0,0, 1,0, 1,0,
    0,0,0,0, 0,0,0,0, 2,0, 16,0,
    'd','a','t','a', 0,0,0,0
  };
  uint32_t riffSize = 36 + dataBytes;
  memcpy(&h[4],  &riffSize, 4);
  uint32_t sr = TEST_SAMPLE_RATE;
  memcpy(&h[24], &sr, 4);
  memcpy(&h[28], &byteRate, 4);
  memcpy(&h[40], &dataBytes, 4);
  f.write(h, sizeof(h));
}

static void storageInit() {
  // Same partition and mount point as Meck Watch, but format-on-fail is OFF:
  // if it will not mount, this test reports it and carries on from RAM.
  fsReady = LittleFS.begin(false, "/maps", 10, "maps");
  if (!fsReady) {
    Serial.println("Flash: LittleFS 'maps' mount FAILED (not formatted; nothing changed)");
    return;
  }
  Serial.printf("Flash: LittleFS 'maps' mounted, %u KB used of %u KB\n",
                (unsigned)(LittleFS.usedBytes() / 1024),
                (unsigned)(LittleFS.totalBytes() / 1024));
  if (!LittleFS.exists(TEST_DIR)) LittleFS.mkdir(TEST_DIR);

  File f = LittleFS.open(TEST_RESULTS_PATH, "r");
  if (f) {
    Serial.println("---- Results from the previous run ----");
    while (f.available()) Serial.write(f.read());
    Serial.println("---------------------------------------");
    f.close();
  }
}

static void saveResults() {
  if (!fsReady) return;
  File f = LittleFS.open(TEST_RESULTS_PATH, "w");
  if (!f) return;
  f.print(results);
  f.close();
}

// =============================================================================
// Mic (I2S_NUM_0, PDM RX) -- settings from LilyGoLib::initMicrophone
// =============================================================================
static bool micStart() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
  cfg.sample_rate = TEST_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 16;
  cfg.dma_buf_len = 1024;
  cfg.use_apll = true;

  if (i2s_driver_install(TEST_MIC_PORT, &cfg, 0, NULL) != ESP_OK) {
    Serial.println("Mic: i2s_driver_install failed");
    return false;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = I2S_PIN_NO_CHANGE;
  pins.ws_io_num    = TEST_MIC_CLOCK;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num  = TEST_MIC_DATA;
  if (i2s_set_pin(TEST_MIC_PORT, &pins) != ESP_OK) {
    Serial.println("Mic: i2s_set_pin failed");
    i2s_driver_uninstall(TEST_MIC_PORT);
    return false;
  }
  return true;
}

static void micStop() {
  i2s_driver_uninstall(TEST_MIC_PORT);
}

// =============================================================================
// Speaker (I2S_NUM_1, standard I2S TX to the MAX98357A)
// =============================================================================
static bool spkStart() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = TEST_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 512;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;

  if (i2s_driver_install(TEST_SPK_PORT, &cfg, 0, NULL) != ESP_OK) {
    Serial.println("Speaker: i2s_driver_install failed");
    return false;
  }
  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;
  pins.bck_io_num   = TEST_SPK_BCLK;
  pins.ws_io_num    = TEST_SPK_WS;
  pins.data_out_num = TEST_SPK_DOUT;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;
  if (i2s_set_pin(TEST_SPK_PORT, &pins) != ESP_OK) {
    Serial.println("Speaker: i2s_set_pin failed");
    i2s_driver_uninstall(TEST_SPK_PORT);
    return false;
  }
  i2s_zero_dma_buffer(TEST_SPK_PORT);
  return true;
}

static void spkStop() {
  // i2s_write returns once the last chunk is queued, not played. 8 DMA
  // buffers x 512 frames at 16 kHz is 256 ms still to play, so wait for it
  // before silencing the port.
  delay(300);
  i2s_zero_dma_buffer(TEST_SPK_PORT);
  delay(50);
  i2s_driver_uninstall(TEST_SPK_PORT);
}

// Writes mono samples to the speaker as identical left/right pairs.
// If drainMic is true, the mic port is read (and discarded) between writes,
// which is the "both ports at once" check. Returns bytes read from the mic.
static uint32_t spkWriteMono(const int16_t* src, uint32_t count, bool drainMic) {
  static int16_t stereo[512 * 2];
  static uint8_t micScratch[1024];
  uint32_t micBytes = 0;
  uint32_t pos = 0;
  while (pos < count) {
    uint32_t n = count - pos;
    if (n > 512) n = 512;
    for (uint32_t i = 0; i < n; i++) {
      stereo[i * 2]     = src[pos + i];
      stereo[i * 2 + 1] = src[pos + i];
    }
    size_t written = 0;
    i2s_write(TEST_SPK_PORT, stereo, n * 4, &written, portMAX_DELAY);
    pos += n;
    if (drainMic) {
      // i2s_read with no wait returns ESP_ERR_TIMEOUT once the DMA queue is
      // empty, but still reports any bytes it copied before that, so count
      // `got` regardless of the return code and stop on a short read.
      for (;;) {
        size_t got = 0;
        i2s_read(TEST_MIC_PORT, micScratch, sizeof(micScratch), &got, 0);
        micBytes += got;
        if (got < sizeof(micScratch)) break;
      }
    }
  }
  return micBytes;
}

// =============================================================================
// Test steps
// =============================================================================
static bool stepRecord() {
  Serial.println();
  Serial.println("Get ready to speak...");
  for (int i = 3; i > 0; i--) {
    Serial.printf("  %d\n", i);
    delay(1000);
  }

  if (!micStart()) {
    note("MIC: FAILED to start");
    return false;
  }

  // Discard ~250 ms of start-up output from the PDM mic.
  {
    uint8_t flush[1024];
    size_t got = 0;
    uint32_t until = millis() + 250;
    while (millis() < until) i2s_read(TEST_MIC_PORT, flush, sizeof(flush), &got, 10);
  }

  Serial.printf(">>> SPEAK NOW (%d seconds) <<<\n", TEST_RECORD_SECONDS);
  recSamples = 0;
  uint32_t t0 = millis();
  while (recSamples < TEST_REC_SAMPLES) {
    size_t got = 0;
    uint32_t want = TEST_REC_SAMPLES - recSamples;
    if (want > 1024) want = 1024;
    esp_err_t e = i2s_read(TEST_MIC_PORT, &recBuf[recSamples], want * 2, &got, 100);
    recSamples += got / 2;
    if (e != ESP_OK && got == 0) break;   // mic delivered nothing for 100 ms
    if (millis() - t0 > (TEST_RECORD_SECONDS + 3) * 1000UL) break;   // safety
  }
  uint32_t elapsed = millis() - t0;
  Serial.println(">>> STOP <<<");
  // Mic is left running on purpose: the playback step reads it at the same
  // time as the speaker plays, to check both I2S ports together.

  // Raw level stats (before normalising)
  int64_t sum = 0;
  int32_t peak = 0;
  for (uint32_t i = 0; i < recSamples; i++) {
    sum += recBuf[i];
    int32_t a = abs((int32_t)recBuf[i]);
    if (a > peak) peak = a;
  }
  int32_t dc = recSamples ? (int32_t)(sum / (int64_t)recSamples) : 0;
  int64_t sq = 0;
  for (uint32_t i = 0; i < recSamples; i++) {
    int32_t v = recBuf[i] - dc;
    sq += (int64_t)v * v;
  }
  float rms = recSamples ? sqrtf((float)sq / recSamples) : 0;

  note("MIC: %lu samples in %lu ms (expected %d ms)",
       (unsigned long)recSamples, (unsigned long)elapsed, TEST_RECORD_SECONDS * 1000);
  note("MIC: raw peak %ld, RMS %.0f, DC offset %ld (full scale 32767)",
       (long)peak, rms, (long)dc);
  if (peak < 100) note("MIC: near-silent -- mic may not be working");

  // Remove DC offset and normalise to 90% of full scale (as Meck does).
  int32_t peak2 = 0;
  for (uint32_t i = 0; i < recSamples; i++) {
    int32_t v = recBuf[i] - dc;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    recBuf[i] = (int16_t)v;
    int32_t a = abs(v);
    if (a > peak2) peak2 = a;
  }
  if (peak2 >= 100) {
    int32_t gain16 = (29491 << 16) / peak2;
    for (uint32_t i = 0; i < recSamples; i++) {
      int32_t v = ((int32_t)recBuf[i] * gain16) >> 16;
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      recBuf[i] = (int16_t)v;
    }
    note("MIC: normalised with gain %.1fx", gain16 / 65536.0f);
  }
  return true;
}

static void stepSaveWav() {
  if (!fsReady) {
    note("FLASH: skipped (LittleFS not mounted)");
    return;
  }
  uint32_t dataBytes = recSamples * 2;
  uint32_t t0 = millis();
  File f = LittleFS.open(TEST_WAV_PATH, "w");
  if (!f) {
    note("FLASH: could not create %s", TEST_WAV_PATH);
    return;
  }
  writeWavHeader(f, dataBytes);
  size_t w = f.write((const uint8_t*)recBuf, dataBytes);
  f.close();
  note("FLASH: saved %s, %lu bytes in %lu ms (%s)", TEST_WAV_PATH,
       (unsigned long)(44 + w), (unsigned long)(millis() - t0),
       w == dataBytes ? "OK" : "SHORT WRITE");
}

static void stepPlayWav() {
  Serial.println("Playing your recording (uncompressed)...");
  if (!spkStart()) {
    note("SPEAKER: FAILED to start");
    micStop();   // still running from stepRecord; free port 0 for the next run
    return;
  }

  uint32_t t0 = millis();
  uint32_t micBytes = 0;
  uint32_t played = 0;
  bool fromFlash = false;

  File f;
  if (fsReady) f = LittleFS.open(TEST_WAV_PATH, "r");
  if (f && f.size() > 44) {
    fromFlash = true;
    f.seek(44);
    static int16_t chunk[512];
    while (f.available()) {
      int got = f.read((uint8_t*)chunk, sizeof(chunk));
      if (got <= 0) break;
      micBytes += spkWriteMono(chunk, got / 2, true);
      played += got / 2;
    }
    f.close();
  } else {
    micBytes += spkWriteMono(recBuf, recSamples, true);
    played = recSamples;
  }
  uint32_t elapsed = millis() - t0;
  spkStop();
  micStop();

  note("PLAYBACK: %lu samples from %s in %lu ms (audio length %lu ms)",
       (unsigned long)played, fromFlash ? "flash" : "RAM",
       (unsigned long)elapsed, (unsigned long)(played * 1000UL / TEST_SAMPLE_RATE));
  note("BOTH PORTS: mic delivered %lu bytes while speaker played (%s)",
       (unsigned long)micBytes, micBytes > 0 ? "both ran together" : "mic gave nothing");
}

// =============================================================================
// Codec2 comparison
//
// One recording, three versions played back to back, all encoded and decoded
// at 80 MHz (the SX1280 watch build speed), all "filtered" (hum filter at
// TEST_HPF_HZ and a low-pass filter before 16 -> 8 kHz, the same low-pass
// smoothing 8 -> 16 kHz on playback, as the voice notes do now):
//
//   A  1200 bps (what the voice notes use now, for comparison)
//   B  700C     (28 bits per 40 ms frame, stored as 4 bytes)
//   C  450      (18 bits per 40 ms frame, stored as 3 bytes)
//
// B and C are for listening only: nothing decodes them yet.
// =============================================================================
#define TEST_C2_SAMPLES_8K  (TEST_REC_SAMPLES / 2)
#define TEST_C2_MAX_BYTES   4096        // 3200 bps x 5 s = 2000 bytes
#define TEST_HPF_HZ         150.0f      // hum filter corner
#define TEST_LPF_HZ         3600.0f     // anti-alias / smoothing corner
#define TEST_FIR_TAPS       31

static int16_t* src8k = NULL;     // 8 kHz input to the encoder (PSRAM)
static int16_t* dec8k = NULL;     // 8 kHz decoder output (PSRAM)
static float    firTaps[TEST_FIR_TAPS];

// Windowed-sinc low-pass (Hamming), designed at the 16 kHz rate, unity gain.
static void firDesign() {
  const int M = (TEST_FIR_TAPS - 1) / 2;
  const float fc = TEST_LPF_HZ / TEST_SAMPLE_RATE;
  float sum = 0;
  for (int n = 0; n < TEST_FIR_TAPS; n++) {
    int k = n - M;
    float sinc = (k == 0) ? 2.0f * fc : sinf(2.0f * PI * fc * k) / (PI * k);
    float win = 0.54f - 0.46f * cosf(2.0f * PI * n / (TEST_FIR_TAPS - 1));
    firTaps[n] = sinc * win;
    sum += firTaps[n];
  }
  for (int n = 0; n < TEST_FIR_TAPS; n++) firTaps[n] /= sum;
}

static inline int16_t clip16(float v) {
  if (v > 32767.0f) return 32767;
  if (v < -32768.0f) return -32768;
  return (int16_t)v;
}

// A: Meck's current 16 -> 8 kHz (average each pair).
static void downPlain() {
  for (uint32_t i = 0; i < TEST_C2_SAMPLES_8K; i++) {
    src8k[i] = (int16_t)(((int32_t)recBuf[i * 2] + recBuf[i * 2 + 1]) / 2);
  }
}

// B-E: high-pass (2nd-order Butterworth, RBJ biquad) at 16 kHz, then the
// FIR low-pass evaluated at every second sample (16 -> 8 kHz).
static void downFiltered() {
  const float w0 = 2.0f * PI * TEST_HPF_HZ / TEST_SAMPLE_RATE;
  const float alpha = sinf(w0) / (2.0f * 0.7071f);
  const float cw = cosf(w0);
  const float a0 = 1.0f + alpha;
  const float b0 = ((1.0f + cw) / 2.0f) / a0, b1 = (-(1.0f + cw)) / a0, b2 = b0;
  const float a1 = (-2.0f * cw) / a0, a2 = (1.0f - alpha) / a0;

  // High-pass into playBuf as 16 kHz scratch (it is refilled on playback).
  float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (uint32_t i = 0; i < recSamples; i++) {
    float x = recBuf[i];
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    playBuf[i] = clip16(y);
  }

  const int M = (TEST_FIR_TAPS - 1) / 2;
  for (uint32_t n = 0; n < TEST_C2_SAMPLES_8K; n++) {
    int32_t centre = (int32_t)(n * 2);
    float acc = 0;
    for (int k = 0; k < TEST_FIR_TAPS; k++) {
      int32_t idx = centre + M - k;
      if (idx >= 0 && idx < (int32_t)recSamples) acc += firTaps[k] * playBuf[idx];
    }
    src8k[n] = clip16(acc);
  }
}

// A: Meck's current 8 -> 16 kHz (repeat each sample).
static uint32_t upPlain(uint32_t n8k) {
  for (uint32_t i = 0; i < n8k; i++) {
    playBuf[i * 2] = dec8k[i];
    playBuf[i * 2 + 1] = dec8k[i];
  }
  return n8k * 2;
}

// B-E: 8 -> 16 kHz by inserting zeros between samples and running the same
// FIR low-pass (x2 gain to make up for the inserted zeros).
static uint32_t upFiltered(uint32_t n8k) {
  const int M = (TEST_FIR_TAPS - 1) / 2;
  uint32_t n16k = n8k * 2;
  for (uint32_t m = 0; m < n16k; m++) {
    float acc = 0;
    for (int k = 0; k < TEST_FIR_TAPS; k++) {
      int32_t idx = (int32_t)m + M - k;               // index in the zero-stuffed signal
      if (idx >= 0 && idx < (int32_t)n16k && (idx & 1) == 0) acc += firTaps[k] * dec8k[idx / 2];
    }
    playBuf[m] = clip16(2.0f * acc);
  }
  return n16k;
}

struct C2Variant {
  char        label;
  int         mode;
  int         bps;
  bool        filtered;
};

static const C2Variant kVariants[] = {
  { 'A', CODEC2_MODE_1200, 1200, true },
  { 'B', CODEC2_MODE_700C,  700, true },
  { 'C', CODEC2_MODE_450,   450, true },
};

static void runVariant(const C2Variant& v) {
  int64_t tPrep = esp_timer_get_time();
  if (v.filtered) downFiltered(); else downPlain();
  uint32_t prepMs = (uint32_t)((esp_timer_get_time() - tPrep) / 1000);

  struct CODEC2* c2 = codec2_create(v.mode);
  if (!c2) {
    note("%c %d: codec2_create FAILED", v.label, v.bps);
    return;
  }
  int frameSamples = codec2_samples_per_frame(c2);       // 320 (40 ms) for 1200, 700C and 450
  int frameBytes = (codec2_bits_per_frame(c2) + 7) / 8;
  uint32_t budgetUs = (uint32_t)frameSamples * 1000000UL / 8000;   // 40 ms or 20 ms

  // Encode
  uint32_t bytes = 0;
  int frames = 0;
  uint64_t encTotal = 0;
  uint32_t encWorst = 0;
  for (uint32_t s = 0; s + frameSamples <= TEST_C2_SAMPLES_8K &&
                       bytes + frameBytes <= TEST_C2_MAX_BYTES; s += frameSamples) {
    int64_t t = esp_timer_get_time();
    codec2_encode(c2, &c2Buf[bytes], &src8k[s]);
    uint32_t us = (uint32_t)(esp_timer_get_time() - t);
    encTotal += us;
    if (us > encWorst) encWorst = us;
    bytes += frameBytes;
    frames++;
  }

  // Decode
  uint32_t out8k = 0;
  uint64_t decTotal = 0;
  uint32_t decWorst = 0;
  for (uint32_t pos = 0; pos + frameBytes <= bytes &&
                         out8k + frameSamples <= TEST_C2_SAMPLES_8K; pos += frameBytes) {
    int64_t t = esp_timer_get_time();
    codec2_decode(c2, &dec8k[out8k], &c2Buf[pos]);
    uint32_t us = (uint32_t)(esp_timer_get_time() - t);
    decTotal += us;
    if (us > decWorst) decWorst = us;
    out8k += frameSamples;
  }
  codec2_destroy(c2);

  uint32_t encAvg = frames ? (uint32_t)(encTotal / frames) : 0;
  uint32_t decAvg = frames ? (uint32_t)(decTotal / frames) : 0;
  note("%c %d bps%s: %d frames of %d ms -> %lu bytes (%d packets of 150)",
       v.label, v.bps, v.filtered ? " filtered" : " as Meck", frames, (int)(budgetUs / 1000),
       (unsigned long)bytes, (int)((bytes + 149) / 150));
  note("%c   encode avg %lu us, worst %lu us; decode avg %lu us, worst %lu us (budget %lu us/frame)",
       v.label, (unsigned long)encAvg, (unsigned long)encWorst,
       (unsigned long)decAvg, (unsigned long)decWorst, (unsigned long)budgetUs);
  int64_t tPost = esp_timer_get_time();
  uint32_t n16k = v.filtered ? upFiltered(out8k) : upPlain(out8k);
  uint32_t postMs = (uint32_t)((esp_timer_get_time() - tPost) / 1000);
  note("%c   before-encode audio prep %lu ms, after-decode prep %lu ms (whole 5 s note)",
       v.label, (unsigned long)prepMs, (unsigned long)postMs);
  saveResults();

  Serial.printf(">>> Now playing %c: %d bps, %s <<<\n", v.label, v.bps,
                v.filtered ? "filtered" : "as Meck does now");
  if (!spkStart()) {
    note("%c: SPEAKER FAILED to start", v.label);
    return;
  }
  spkWriteMono(playBuf, n16k, false);
  spkStop();
  delay(1500);   // gap between versions
}

static void stepCodec2Compare() {
  Serial.println();
  Serial.println("Codec2 comparison: three versions, A to C (1200, 700C, 450), 1.5 s gap between each.");
  for (const C2Variant& v : kVariants) {
    note("CODEC2: starting %c (%d bps)", v.label, v.bps);
    saveResults();
    runVariant(v);
  }
  Serial.println("Done.");
}

static void runTest() {
  runCount++;
  results = "";
  note("==== Meck Watch audio test, run %d ====", runCount);
  note("CPU %d MHz, free PSRAM %lu KB", getCpuFrequencyMhz(),
       (unsigned long)(ESP.getFreePsram() / 1024));
  if (pmu) note("PMU: DLDO1 (speaker) %s, %u mV",
                pmu->isEnableDLDO1() ? "ON" : "OFF", pmu->getDLDO1Voltage());

  saveResults();
  if (stepRecord()) {
    saveResults();
    stepSaveWav();
    saveResults();
    stepPlayWav();
    saveResults();
    stepCodec2Compare();   // saves its own progress before and after each version
    saveResults();
  }

  if (fsReady) {
    note("FLASH: LittleFS now %u KB used of %u KB",
         (unsigned)(LittleFS.usedBytes() / 1024), (unsigned)(LittleFS.totalBytes() / 1024));
  }
  note("==== End of run %d ====", runCount);
  saveResults();
  Serial.println("Press the PWR key again to repeat.");
}

// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(1500);   // give the USB serial monitor time to attach
  setCpuFrequencyMhz(TEST_CPU_NORMAL);

  Serial.println();
  Serial.println("=== Meck Watch audio test (T-Watch S3, SX1280 family) ===");
  Serial.println("Radio is not used. Screen stays dark.");

  Wire.begin(TEST_I2C_SDA, TEST_I2C_SCL);
  powerInit();

  recBuf  = (int16_t*)ps_calloc(TEST_REC_SAMPLES, sizeof(int16_t));
  playBuf = (int16_t*)ps_calloc(TEST_REC_SAMPLES, sizeof(int16_t));
  c2Buf   = (uint8_t*)ps_calloc(TEST_C2_MAX_BYTES, 1);
  src8k   = (int16_t*)ps_calloc(TEST_C2_SAMPLES_8K, sizeof(int16_t));
  dec8k   = (int16_t*)ps_calloc(TEST_C2_SAMPLES_8K, sizeof(int16_t));
  if (!recBuf || !playBuf || !c2Buf || !src8k || !dec8k) {
    Serial.println("PSRAM allocation FAILED -- cannot run the test");
  }
  firDesign();

  storageInit();
  Serial.println("Ready. Press the side PWR key briefly to start a test run.");
}

void loop() {
  if (recBuf && playBuf && c2Buf && src8k && dec8k && pwrKeyClicked()) {
    runTest();
  }
  delay(30);
}
