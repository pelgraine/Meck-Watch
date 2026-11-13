#pragma once

// =============================================================================
// WatchVoiceAudio -- mic and speaker for the T-Watch S3 voice notes
// (SX1280 builds only, MECK_WATCH_VOICE).
//
// Hardware, as measured with the meck_twatch_s3_1280_audiotest build (Nov 2026):
//   PDM microphone  CLK GPIO44, DATA GPIO47, on I2S_NUM_0 (PDM RX is only
//                   available on port 0). Settings follow LilyGo's own
//                   LilyGoLib::initMicrophone (16 kHz, right channel, PCM short).
//   MAX98357A amp   BCLK GPIO48, LRCLK GPIO15, DIN GPIO46, on I2S_NUM_1, with
//                   its VDD on the AXP2101 DLDO1 rail (schematic sheet 6,
//                   SPK_VDD). power_init() leaves DLDO1 off; it is switched on
//                   only while a note is playing and off again afterwards.
// The mic and the speaker ran on their two ports at the same time in the test.
//
// Both ports are driven with non-blocking reads/writes from the UI poll, so the
// mesh loop keeps running while a note records or plays.
// =============================================================================

#if defined(MECK_WATCH_VOICE)

#include <Arduino.h>
#include <driver/i2s.h>
#include "target.h"   // board (TWatchS3Board) for the DLDO1 speaker rail

#define WVA_SAMPLE_RATE  16000
#define WVA_MIC_PORT     I2S_NUM_0
#define WVA_SPK_PORT     I2S_NUM_1
#define WVA_MIC_CLOCK    44
#define WVA_MIC_DATA     47
#define WVA_SPK_BCLK     48
#define WVA_SPK_WS       15
#define WVA_SPK_DOUT     46

class WatchVoiceAudio {
  bool _micOn = false;
  bool _spkOn = false;

public:
  bool micOn() const { return _micOn; }
  bool spkOn() const { return _spkOn; }

  bool micStart() {
    if (_micOn) return true;
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    cfg.sample_rate = WVA_SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_PCM_SHORT;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 16;     // 16 x 1024 samples = ~1 s of headroom
    cfg.dma_buf_len = 1024;
    cfg.use_apll = true;
    if (i2s_driver_install(WVA_MIC_PORT, &cfg, 0, NULL) != ESP_OK) {
      Serial.println("Voice: mic i2s_driver_install failed");
      return false;
    }
    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = I2S_PIN_NO_CHANGE;
    pins.ws_io_num    = WVA_MIC_CLOCK;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = WVA_MIC_DATA;
    if (i2s_set_pin(WVA_MIC_PORT, &pins) != ESP_OK) {
      Serial.println("Voice: mic i2s_set_pin failed");
      i2s_driver_uninstall(WVA_MIC_PORT);
      return false;
    }
    _micOn = true;
    return true;
  }

  void micStop() {
    if (!_micOn) return;
    i2s_driver_uninstall(WVA_MIC_PORT);
    _micOn = false;
  }

  // Copies whatever the mic DMA has ready (no waiting) into dst. Returns the
  // number of samples copied, at most maxSamples.
  uint32_t micRead(int16_t* dst, uint32_t maxSamples) {
    if (!_micOn || maxSamples == 0) return 0;
    uint32_t total = 0;
    while (total < maxSamples) {
      size_t got = 0;
      uint32_t want = maxSamples - total;
      if (want > 1024) want = 1024;
      i2s_read(WVA_MIC_PORT, &dst[total], want * 2, &got, 0);
      total += got / 2;
      if (got < want * 2) break;   // DMA queue drained
    }
    return total;
  }

  bool spkStart() {
    if (_spkOn) return true;
    XPowersAXP2101* pmu = board.getPMU();
    if (pmu) pmu->enableDLDO1();
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = WVA_SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = 16;     // 16 x 512 frames = ~0.5 s queued ahead
    cfg.dma_buf_len = 512;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;
    if (i2s_driver_install(WVA_SPK_PORT, &cfg, 0, NULL) != ESP_OK) {
      Serial.println("Voice: speaker i2s_driver_install failed");
      if (pmu) pmu->disableDLDO1();
      return false;
    }
    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;
    pins.bck_io_num   = WVA_SPK_BCLK;
    pins.ws_io_num    = WVA_SPK_WS;
    pins.data_out_num = WVA_SPK_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(WVA_SPK_PORT, &pins) != ESP_OK) {
      Serial.println("Voice: speaker i2s_set_pin failed");
      i2s_driver_uninstall(WVA_SPK_PORT);
      if (pmu) pmu->disableDLDO1();
      return false;
    }
    i2s_zero_dma_buffer(WVA_SPK_PORT);
    _spkOn = true;
    return true;
  }

  void spkStop() {
    if (!_spkOn) return;
    i2s_zero_dma_buffer(WVA_SPK_PORT);
    i2s_driver_uninstall(WVA_SPK_PORT);
    XPowersAXP2101* pmu = board.getPMU();
    if (pmu) pmu->disableDLDO1();
    _spkOn = false;
  }

  // Queues as many mono samples as fit right now (no waiting), each written as
  // an identical left/right pair. Returns the number of samples taken.
  uint32_t spkWrite(const int16_t* src, uint32_t count) {
    if (!_spkOn || count == 0) return 0;
    static int16_t stereo[256 * 2];
    uint32_t done = 0;
    while (done < count) {
      uint32_t n = count - done;
      if (n > 256) n = 256;
      for (uint32_t i = 0; i < n; i++) {
        stereo[i * 2]     = src[done + i];
        stereo[i * 2 + 1] = src[done + i];
      }
      size_t written = 0;
      i2s_write(WVA_SPK_PORT, stereo, n * 4, &written, 0);
      done += written / 4;
      if (written < n * 4) break;   // DMA queue full
    }
    return done;
  }
};

#endif  // MECK_WATCH_VOICE
