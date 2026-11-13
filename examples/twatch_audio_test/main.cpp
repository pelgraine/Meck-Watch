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
//   5. How long does Codec2 1200 take per 40 ms frame at 80, 160 and 240 MHz?
//   6. What does the Codec2 version sound like on the watch speaker?
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

#define TEST_C2_MODE        CODEC2_MODE_1200
#define TEST_C2_FRAME_MS    40

#define TEST_CPU_NORMAL     80          // what the SX1280 watch builds run at
#define TEST_CPU_MID        160         // headroom check
#define TEST_CPU_HIGH       240         // headroom check (ESP32-S3 maximum)
// 40 MHz was dropped: on hardware the watch reset (TG1WDT_SYS_RST) right after
// switching 80 -> 40 MHz in this test.

#define TEST_DIR            "/voicetest"
#define TEST_WAV_PATH       "/voicetest/rec.wav"
#define TEST_C2_PATH        "/voicetest/rec.c2"
#define TEST_RESULTS_PATH   "/voicetest/results.txt"

// ---- State ------------------------------------------------------------------
static XPowersAXP2101* pmu = NULL;
static bool fsReady = false;
static int  runCount = 0;

static int16_t* recBuf  = NULL;   // 16 kHz recording (PSRAM)
static int16_t* playBuf = NULL;   // 16 kHz decoded Codec2 for playback (PSRAM)
static uint8_t* c2Buf   = NULL;   // encoded Codec2 bytes (PSRAM)
static uint32_t recSamples = 0;
static uint32_t c2Bytes = 0;
static int      c2Frames = 0;

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

// Encodes the whole recording once at the given CPU speed.
// Nothing is printed while the CPU is at the test speed.
static void encodeAt(int mhz, uint32_t* avgUs, uint32_t* maxUs) {
  struct CODEC2* c2 = codec2_create(TEST_C2_MODE);
  if (!c2) {
    *avgUs = 0;
    *maxUs = 0;
    return;
  }
  int frameSamples = codec2_samples_per_frame(c2);          // 320 at 8 kHz
  int frameBytes = (codec2_bits_per_frame(c2) + 7) / 8;      // 6
  int srcPerFrame = frameSamples * 2;                        // 640 at 16 kHz
  static int16_t frame[320];

  int before = getCpuFrequencyMhz();
  if (mhz != before) {
    note("CPU: changing %d -> %d MHz", before, mhz);
    saveResults();
  }
  Serial.flush();
  setCpuFrequencyMhz(mhz);

  uint64_t total = 0;
  uint32_t worst = 0;
  c2Bytes = 0;
  c2Frames = 0;
  for (uint32_t src = 0; src + srcPerFrame <= recSamples; src += srcPerFrame) {
    for (int i = 0; i < frameSamples; i++) {
      frame[i] = (int16_t)(((int32_t)recBuf[src + i * 2] + recBuf[src + i * 2 + 1]) / 2);
    }
    int64_t t = esp_timer_get_time();
    codec2_encode(c2, &c2Buf[c2Bytes], frame);
    uint32_t us = (uint32_t)(esp_timer_get_time() - t);
    total += us;
    if (us > worst) worst = us;
    c2Bytes += frameBytes;
    c2Frames++;
  }

  setCpuFrequencyMhz(TEST_CPU_NORMAL);
  delay(20);
  if (mhz != before) note("CPU: back to %d MHz", getCpuFrequencyMhz());

  codec2_destroy(c2);
  *avgUs = c2Frames ? (uint32_t)(total / c2Frames) : 0;
  *maxUs = worst;
}

// Each pass is announced and saved to results.txt before it starts, and its
// result is saved as soon as it finishes, so a crash part-way through still
// leaves a record of how far it got (printed again at the next boot).
static void stepCodec2() {
  Serial.println("Timing Codec2 1200 encoding...");
  uint32_t budget = TEST_C2_FRAME_MS * 1000;
  uint32_t avgUs, maxUs;

  // ---- Pass 1: 80 MHz (no speed change) ----
  note("CODEC2: starting %d MHz pass", TEST_CPU_NORMAL);
  saveResults();
  encodeAt(TEST_CPU_NORMAL, &avgUs, &maxUs);
  note("CODEC2: %d frames -> %lu bytes (%d packets of 150)", c2Frames,
       (unsigned long)c2Bytes, (int)((c2Bytes + 149) / 150));
  note("CODEC2 @%d MHz: avg %lu us, worst %lu us per frame (%s; budget %lu us)",
       TEST_CPU_NORMAL, (unsigned long)avgUs, (unsigned long)maxUs,
       maxUs < budget ? "keeps up" : "SLOWER than real time", (unsigned long)budget);

  // Save the 80 MHz pass's Codec2 file now, so it exists even if a
  // higher-speed pass does not finish.
  if (fsReady) {
    File f = LittleFS.open(TEST_C2_PATH, "w");
    if (f) {
      size_t w = f.write(c2Buf, c2Bytes);
      f.close();
      note("FLASH: saved %s, %lu bytes", TEST_C2_PATH, (unsigned long)w);
    }
  }
  saveResults();

  // ---- Passes 2 and 3: 160 MHz, then 240 MHz (headroom check) ----
  const int higher[] = { TEST_CPU_MID, TEST_CPU_HIGH };
  for (int mhz : higher) {
    note("CODEC2: starting %d MHz pass", mhz);
    saveResults();
    encodeAt(mhz, &avgUs, &maxUs);
    note("CODEC2 @%d MHz: avg %lu us, worst %lu us per frame (%s)",
         mhz, (unsigned long)avgUs, (unsigned long)maxUs,
         maxUs < budget ? "keeps up" : "SLOWER than real time");
    saveResults();
  }
}

static void stepPlayCodec2() {
  // Read the Codec2 file back from flash if it is there, so this also proves
  // the saved .c2 file is usable.
  uint32_t n = c2Bytes;
  bool fromFlash = false;
  if (fsReady) {
    File f = LittleFS.open(TEST_C2_PATH, "r");
    if (f) {
      n = f.read(c2Buf, c2Bytes);
      f.close();
      fromFlash = true;
    }
  }

  struct CODEC2* c2 = codec2_create(TEST_C2_MODE);
  if (!c2) {
    note("CODEC2: decoder create FAILED");
    return;
  }
  int frameSamples = codec2_samples_per_frame(c2);
  int frameBytes = (codec2_bits_per_frame(c2) + 7) / 8;
  static int16_t frame[320];

  uint32_t out = 0;
  int64_t t = esp_timer_get_time();
  for (uint32_t pos = 0; pos + frameBytes <= n && out + frameSamples * 2 <= TEST_REC_SAMPLES;
       pos += frameBytes) {
    codec2_decode(c2, frame, &c2Buf[pos]);
    for (int i = 0; i < frameSamples; i++) {   // 8 kHz -> 16 kHz (as Meck)
      playBuf[out++] = frame[i];
      playBuf[out++] = frame[i];
    }
  }
  uint32_t decodeMs = (uint32_t)((esp_timer_get_time() - t) / 1000);
  codec2_destroy(c2);
  note("CODEC2: decoded %lu bytes from %s in %lu ms at %d MHz",
       (unsigned long)n, fromFlash ? "flash" : "RAM", (unsigned long)decodeMs, TEST_CPU_NORMAL);

  Serial.println("Playing the Codec2 version (what would go over LoRa)...");
  if (!spkStart()) {
    note("SPEAKER: FAILED to start for Codec2 playback");
    return;
  }
  spkWriteMono(playBuf, out, false);
  spkStop();
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
    stepCodec2();       // saves its own progress before and after each pass
    stepPlayCodec2();
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
  c2Buf   = (uint8_t*)ps_calloc((TEST_RECORD_SECONDS * 1000 / TEST_C2_FRAME_MS) * 8, 1);
  if (!recBuf || !playBuf || !c2Buf) {
    Serial.println("PSRAM allocation FAILED -- cannot run the test");
  }

  storageInit();
  Serial.println("Ready. Press the side PWR key briefly to start a test run.");
}

void loop() {
  if (recBuf && playBuf && c2Buf && pwrKeyClicked()) {
    runTest();
  }
  delay(30);
}
