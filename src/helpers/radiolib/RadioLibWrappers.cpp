#define RADIOLIB_STATIC_ONLY 1
#include "RadioLibWrappers.h"

#if defined(MECK_DUTY_DIAG)
#include <LittleFS.h>   // diag v2 file logger; mounted by main.cpp before the_mesh.begin()
#endif

#define STATE_IDLE       0
#define STATE_RX         1
#define STATE_TX_WAIT    3
#define STATE_TX_DONE    4
#define STATE_INT_READY 16

#define NUM_NOISE_FLOOR_SAMPLES  64
#define SAMPLING_THRESHOLD  14

// Re-arm watchdog cadence while RX duty-cycle mode is on (see loop()). 60s
// measured at no detectable power impact by the duty diag v2 discharge logs.
#define DUTY_REARM_INTERVAL_MS  60000UL

static volatile uint8_t state = STATE_IDLE;

#if defined(MECK_DUTY_DIAG)
// Temporary diagnostics for the RX duty-cycle drain investigation -- remove
// once concluded. Enable with -D MECK_DUTY_DIAG on the env. Prints a DDIAG
// line every 10s; every 6th line (60s) probes the chip's actual operating
// mode via GetStatus (read-only in v2.1 -- no re-arm).
// Optional second flag -D MECK_DUTY_DIAG_PULLDOWN adds a weak pulldown on
// DIO1 -- see begin().
static volatile uint32_t diag_isr = 0;   // DIO1 interrupts (RxDone + TxDone + phantoms)
static uint32_t diag_arms = 0;           // armDutyCycleRx() invocations
static uint32_t diag_fallbacks = 0;      // arms that fell back to continuous RX
static uint32_t diag_rx_us = 0;          // last computed listen window
static uint32_t diag_slp_us = 0;         // last computed sleep window (as passed;
                                         // RadioLib deducts tcxoDelay+1000us internally)
static int16_t  diag_last_arm_err = 0;   // last startReceiveDutyCycle return code
static bool     diag_first_arm = true;

// --- diag v2.1: on-battery discharge logger -----------------------------
// Appends CSV records to LittleFS so an unattended, worn, on-battery run can
// be read back over serial afterwards (the file is dumped at every boot).
// Record types: H header, B boot marker, E duty-toggle event, L 60s sample.
// v2.1: the 60s chip probe is READ-ONLY (v2's forced re-arm removed), so any
// drain state that only develops under sustained duty cycling can take hold.
#define DUTY_DIAG_FILE       "/duty_diag.csv"
#define DUTY_DIAG_MAX_BYTES  65536
static bool diag_fs_ok = false;
static int  diag_last_evt_duty = -1;

static void diagAppendLine(const char* line) {
  if (!diag_fs_ok) return;
  File f = LittleFS.open(DUTY_DIAG_FILE, FILE_APPEND);
  if (f) { f.println(line); f.close(); }
}
// ------------------------------------------------------------------------
#endif

// this function is called when a complete packet
// is transmitted by the module
static 
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
#if defined(MECK_DUTY_DIAG)
  diag_isr++;
#endif
  // we sent a packet, set the flag
  state |= STATE_INT_READY;
}

void RadioLibWrapper::begin() {
  _radio->setPacketReceivedAction(setFlag);  // this is also SentComplete interrupt

#if defined(MECK_DUTY_DIAG_PULLDOWN) && defined(P_LORA_DIO_1)
  // Diagnostic: weak pulldown on DIO1. The SX1262's DIOs go high-impedance
  // during the sleep phase of SetRxDutyCycle, leaving this input floating for
  // every nap window; if the duty-cycle drain is a floating-input interrupt
  // storm, this kills it. The chip drives DIO1 push-pull while awake, so real
  // IRQs are unaffected.
  pinMode(P_LORA_DIO_1, INPUT_PULLDOWN);
#endif

#if defined(MECK_DUTY_DIAG)
  // diag v2 file logger init. LittleFS is mounted by main.cpp (setup, before
  // the_mesh.begin() reaches here), so only probe it. Dump any existing log
  // to serial for collection, rotate if oversized, then mark this boot.
  {
    File probe = LittleFS.open(DUTY_DIAG_FILE, FILE_READ);
    if (probe) {
      size_t sz = probe.size();
      Serial.printf("=== DUTY_DIAG FILE DUMP BEGIN (%s, %u bytes) ===\n",
                    DUTY_DIAG_FILE, (unsigned)sz);
      while (probe.available()) {
        Serial.write((uint8_t)probe.read());
      }
      probe.close();
      Serial.println("=== DUTY_DIAG FILE DUMP END ===");
      diag_fs_ok = true;
      if (sz > DUTY_DIAG_MAX_BYTES) {
        LittleFS.remove(DUTY_DIAG_FILE);
        Serial.println("DUTY_DIAG: log exceeded 64KB, rotated (deleted)");
      }
    } else {
      File f = LittleFS.open(DUTY_DIAG_FILE, FILE_WRITE);   // create; also proves FS usable
      if (f) {
        f.close();
        diag_fs_ok = true;
      } else {
        Serial.println("DUTY_DIAG: LittleFS unavailable, file logging disabled");
      }
    }
    if (diag_fs_ok) {
      File chk = LittleFS.open(DUTY_DIAG_FILE, FILE_READ);
      bool empty = (!chk || chk.size() == 0);
      if (chk) chk.close();
      if (empty) diagAppendLine("H,rec,millis,duty,lvl,mV,pct,isr,arms,fb,nrecv,nsent,chip");
      char line[48];
      snprintf(line, sizeof(line), "B,%lu", millis());
      diagAppendLine(line);
    }
  }
#endif

  state = STATE_IDLE;

  if (_board->getStartupReason() == BD_STARTUP_RX_PACKET) {  // received a LoRa packet (while in deep sleep)
    setFlag(); // LoRa packet is already received
  }

  _noise_floor = 0;
  _threshold = 0;
  _cad_enabled = false;

  // start average out some samples
  _num_floor_samples = 0;
  _floor_sample_sum = 0;
}

void RadioLibWrapper::idle() {
  _radio->standby();
  state = STATE_IDLE;   // need another startReceive()
}

void RadioLibWrapper::triggerNoiseFloorCalibrate(int threshold) {
  _threshold = threshold;
  if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES) {  // ignore trigger if currently sampling
    _num_floor_samples = 0;
    _floor_sample_sum = 0;
  }
}

void RadioLibWrapper::resetAGC() {
  // make sure we're not mid-receive of packet!
  if ((state & STATE_INT_READY) != 0 || isReceivingPacket()) return;

  // NOTE: according to higher powers, just issuing RadioLib's startReceive() will reset the AGC.
  //      revisit this if a better impl is discovered.
  state = STATE_IDLE;   // trigger a startReceive()
}

void RadioLibWrapper::loop() {
#if defined(MECK_DUTY_DIAG)
  static unsigned long diag_next_print = 0;
  static uint8_t diag_tick = 0;
  if (millis() >= diag_next_print) {
    diag_next_print = millis() + 10000;
    // File log: duty-toggle transitions, checked at 10s resolution so an
    // unattended run records when the state changed.
    if ((int)_dutyEnabled != diag_last_evt_duty) {
      diag_last_evt_duty = (int)_dutyEnabled;
      char ev[48];
      snprintf(ev, sizeof(ev), "E,%lu,duty=%d", millis(), (int)_dutyEnabled);
      diagAppendLine(ev);
    }
    char chip[8] = "-";
    if (++diag_tick >= 6) {   // every 60s: probe the chip's actual mode
      diag_tick = 0;
      uint8_t st = 0;
      int16_t perr = ((SX126x*)_radio)->mod->SPIreadStream(
          (uint16_t)RADIOLIB_SX126X_CMD_GET_STATUS, &st, 1);
      if (perr == RADIOLIB_ERR_NONE) {
        uint8_t mode = (st >> 4) & 0x07;
        const char* names[8] = { "?0", "?1", "STBY_RC", "STBY_XO", "FS", "RX", "TX", "?7" };
        snprintf(chip, sizeof(chip), "%s", names[mode]);
      } else {
        snprintf(chip, sizeof(chip), "e%d", (int)perr);
      }
      // v2.1: probe is read-only -- no forced re-arm. This lets any state
      // that only develops under sustained, uninterrupted duty cycling
      // (which v2's 60s re-arm would have kept resetting) take hold.
      // File log: one full sample per minute, aligned with the chip probe.
      char rec[160];
      snprintf(rec, sizeof(rec), "L,%lu,%d,%u,%u,%u,%lu,%lu,%lu,%lu,%lu,%s",
               millis(), (int)_dutyEnabled, (unsigned)_dutyLevel,
               (unsigned)_board->getBattMilliVolts(),
               (unsigned)_board->getBatteryPercent(),
               (unsigned long)diag_isr, (unsigned long)diag_arms,
               (unsigned long)diag_fallbacks,
               (unsigned long)n_recv, (unsigned long)n_sent, chip);
      diagAppendLine(rec);
    }
    Serial.printf("DDIAG t=%lus duty=%d lvl=%u isr=%lu arms=%lu fb=%lu lastErr=%d rx=%luus slp=%luus nrecv=%lu nsent=%lu st=%u chip=%s\n",
                  millis() / 1000UL, (int)_dutyEnabled, (unsigned)_dutyLevel,
                  (unsigned long)diag_isr, (unsigned long)diag_arms,
                  (unsigned long)diag_fallbacks, (int)diag_last_arm_err,
                  (unsigned long)diag_rx_us, (unsigned long)diag_slp_us,
                  (unsigned long)n_recv, (unsigned long)n_sent,
                  (unsigned)state, chip);
  }
#endif
#if defined(MECK_RX_DUTY_CYCLE)
  // Apply duty-toggle changes immediately: the settings toggle only flips
  // _dutyEnabled, and without this the newly selected mode would not take
  // effect until the next packet or TX forces a re-arm -- on a quiet mesh,
  // potentially hours. Nudging STATE_RX -> STATE_IDLE makes the next
  // recvRaw() pass re-arm in the new mode; every other state already
  // converges through a fresh re-arm when it completes. Also covers duty
  // enabled from saved prefs at boot.
  {
    static int last_duty_applied = -1;
    if ((int)_dutyEnabled != last_duty_applied) {
      last_duty_applied = (int)_dutyEnabled;
      if (state == STATE_RX) {
        state = STATE_IDLE;
      }
    }
  }
  // Duty-cycle RX sleeps the radio between listen windows, so RSSI floor
  // sampling would read garbage. Skip sampling while the toggle is on; when
  // it is off, sample the noise floor normally.
  if (_dutyEnabled) {
    // Re-arm watchdog: a false preamble detect (noise, foreign LoRa traffic,
    // a failed sync) ends the SX1262's SetRxDutyCycle sequence with
    // RxTimeout, which is not mapped to DIO1 -- the chip parks deaf in
    // STDBY_RC with no interrupt to report it. The AGC reset and RX-stuck
    // watchdog are both disabled in duty mode, so this periodic nudge is the
    // only recovery path. Cost: one SetRxDutyCycle command per interval,
    // measured at no detectable power impact (duty diag v2, 60s cadence).
    static unsigned long next_duty_rearm = 0;
    if (millis() >= next_duty_rearm) {
      next_duty_rearm = millis() + DUTY_REARM_INTERVAL_MS;
      if (state == STATE_RX) {
        state = STATE_IDLE;
      }
    }
    return;
  }
#endif
  if (state == STATE_RX && _num_floor_samples < NUM_NOISE_FLOOR_SAMPLES) {
    if (!isReceivingPacket()) {
      int rssi = getCurrentRSSI();
      if (rssi < _noise_floor + SAMPLING_THRESHOLD) {  // only consider samples below current floor + sampling THRESHOLD
        _num_floor_samples++;
        _floor_sample_sum += rssi;
      }
    }
  } else if (_num_floor_samples >= NUM_NOISE_FLOOR_SAMPLES && _floor_sample_sum != 0) {
    _noise_floor = _floor_sample_sum / NUM_NOISE_FLOOR_SAMPLES;
    if (_noise_floor < -120) {
      _noise_floor = -120;    // clamp to lower bound of -120dBi
    }
    _floor_sample_sum = 0;

    #ifdef MESH_DEBUG_NOISE_FLOOR
    MESH_DEBUG_PRINTLN("RadioLibWrapper: noise_floor = %d", (int)_noise_floor);
    #endif
  }
}

#if defined(MECK_RX_DUTY_CYCLE)
// Nap depths -- see setDutyLevel(). Listen window is held long enough to detect
// a preamble; the sleep window sets how deeply we nap. On short-preamble senders
// the miss rate tracks the asleep fraction ~1:1, which is the user-facing dial.
#define MECK_RX_DUTY_LISTEN_SYM   9
static const uint8_t MECK_RX_DUTY_SLEEP_SYM[3] = { 4, 9, 16 };  // Light, Medium, Aggressive

// Arm the SX126x's own hardware RX duty cycle for the current SF/BW and the
// selected nap depth. The chip listens for rxPeriod, naps for sleepPeriod, and
// holds in RX by itself once it detects a preamble (so a detected packet is not
// cut off). If the sleep window is too short for the chip's transition time
// (very short-symbol presets), RadioLib rejects it and we fall back to plain
// continuous RX -- i.e. no napping, never a dropped packet.
int RadioLibWrapper::armDutyCycleRx() {
  SX126x* r = (SX126x*)_radio;
  uint8_t sf = r->spreadingFactor;
  float   bw = r->bandwidthKhz;
  if (sf < 7 || bw <= 0.0f) return r->startReceive();   // safety -> continuous

  // symbol time (us) = (2^SF / BW_kHz) * 1000
  uint32_t tsym_us = (uint32_t)((float)((uint32_t)1 << sf) / bw * 1000.0f);
  uint8_t  lvl = (_dutyLevel > 2) ? 2 : _dutyLevel;
  uint32_t rx_us    = tsym_us * MECK_RX_DUTY_LISTEN_SYM;
  uint32_t sleep_us = tsym_us * MECK_RX_DUTY_SLEEP_SYM[lvl];

  int err = r->startReceiveDutyCycle(rx_us, sleep_us);
#if defined(MECK_DUTY_DIAG)
  diag_arms++;
  diag_rx_us = rx_us;
  diag_slp_us = sleep_us;
  diag_last_arm_err = err;
  if (err != RADIOLIB_ERR_NONE) diag_fallbacks++;
  if (diag_first_arm) {
    diag_first_arm = false;
    Serial.printf("DDIAG first arm: sf=%u bw=%ukHz tsym=%uus rx=%uus slp=%uus err=%d%s\n",
                  (unsigned)sf, (unsigned)bw, (unsigned)tsym_us,
                  (unsigned)rx_us, (unsigned)sleep_us, (int)err,
                  (err == RADIOLIB_ERR_NONE) ? " (duty ARMED)" : " (FELL BACK to continuous RX)");
  }
#endif
  if (err != RADIOLIB_ERR_NONE) {
    err = r->startReceive();   // window too short for this preset -> continuous RX
  }
  return err;
}
#endif

void RadioLibWrapper::startRecv() {
  int err;
#if defined(MECK_RX_DUTY_CYCLE)
  if (_dutyEnabled) {
    err = armDutyCycleRx();
  } else
#endif
  {
    err = _radio->startReceive();
  }
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_RX;
  } else {
    MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
  }
}

bool RadioLibWrapper::isInRecvMode() const {
  return (state & ~STATE_INT_READY) == STATE_RX;
}

int RadioLibWrapper::recvRaw(uint8_t* bytes, int sz) {
  int len = 0;
  if (state & STATE_INT_READY) {
    len = _radio->getPacketLength();
    if (len > 0) {
      if (len > sz) { len = sz; }
      int err = _radio->readData(bytes, len);
      if (err != RADIOLIB_ERR_NONE) {
        MESH_DEBUG_PRINTLN("RadioLibWrapper: error: readData(%d)", err);
        len = 0;
      } else {
      //  Serial.print("  readData() -> "); Serial.println(len);
        n_recv++;
      }
    }
    state = STATE_IDLE;   // need another startReceive()
  }

  if (state != STATE_RX) {
    int err;
#if defined(MECK_RX_DUTY_CYCLE)
    if (_dutyEnabled) {
      err = armDutyCycleRx();
    } else
#endif
    {
      err = _radio->startReceive();
    }
    if (err == RADIOLIB_ERR_NONE) {
      state = STATE_RX;
    } else {
      MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startReceive(%d)", err);
    }
  }
  return len;
}

uint32_t RadioLibWrapper::getEstAirtimeFor(int len_bytes) {
  return _radio->getTimeOnAir(len_bytes) / 1000;
}

bool RadioLibWrapper::startSendRaw(const uint8_t* bytes, int len) {
  _board->onBeforeTransmit();
  int err = _radio->startTransmit((uint8_t *) bytes, len);
  if (err == RADIOLIB_ERR_NONE) {
    state = STATE_TX_WAIT;
    return true;
  }
  MESH_DEBUG_PRINTLN("RadioLibWrapper: error: startTransmit(%d)", err);
  idle();   // trigger another startRecv()
  _board->onAfterTransmit();
  return false;
}

bool RadioLibWrapper::isSendComplete() {
  if (state & STATE_INT_READY) {
    state = STATE_IDLE;
    n_sent++;
    return true;
  }
  return false;
}

void RadioLibWrapper::onSendFinished() {
  _radio->finishTransmit();
  _board->onAfterTransmit();
  state = STATE_IDLE;
}

int16_t RadioLibWrapper::performChannelScan() {
  return _radio->scanChannel();
}

bool RadioLibWrapper::isChannelActive() {
  // int.thresh: RSSI-based interference detection (relative to noise floor)
  if (_threshold != 0 && getCurrentRSSI() > _noise_floor + _threshold) return true;

  // cad: hardware channel activity detection (upstream MeshCore; app CLI "set cad on")
  if (_cad_enabled) {
    int16_t result = performChannelScan();
    // scanChannel() triggers DIO interrupt (CAD done) which sets STATE_INT_READY
    // via setFlag() ISR. Clear it before restarting RX so recvRaw() doesn't
    // try to read a non-existent packet and count a spurious recv error.
    state = STATE_IDLE;
    startRecv();
    if (result != RADIOLIB_CHANNEL_FREE) return true;
  }

  return false;
}

float RadioLibWrapper::getLastRSSI() const {
  return _radio->getRSSI();
}
float RadioLibWrapper::getLastSNR() const {
  return _radio->getSNR();
}

// Approximate SNR threshold per SF for successful reception (based on Semtech datasheets)
static float snr_threshold[] = {
    -7.5,  // SF7 needs at least -7.5 dB SNR
    -10,   // SF8 needs at least -10 dB SNR
    -12.5, // SF9 needs at least -12.5 dB SNR
    -15,  // SF10 needs at least -15 dB SNR
    -17.5,// SF11 needs at least -17.5 dB SNR
    -20   // SF12 needs at least -20 dB SNR
};
  
float RadioLibWrapper::packetScoreInt(float snr, int sf, int packet_len) {
  if (sf < 7) return 0.0f;
  
  if (snr < snr_threshold[sf - 7]) return 0.0f;    // Below threshold, no chance of success

  auto success_rate_based_on_snr = (snr - snr_threshold[sf - 7]) / 10.0;
  auto collision_penalty = 1 - (packet_len / 256.0);   // Assuming max packet of 256 bytes

  return max(0.0, min(1.0, success_rate_based_on_snr * collision_penalty));
}

PacketMillis RadioLibWrapper::calcMaxPacketMillis(uint8_t sf, float bw, uint8_t cr, uint8_t preambleSymbols) {
  // based on RadioLib's calculateTimeOnAir()
  uint32_t tsym_us = ((uint32_t)10000 << sf) / (bw * 10);
  uint32_t sfCoeff1_x4 = (sf == 5 || sf == 6) ? 25 : 17; // 6.25 : 4.25, semtech magic numbers to account for sync word + sfd

  // preamble + syncword + sfd + header
  uint32_t preamble_us = (((preambleSymbols + 8) * 4 + sfCoeff1_x4) * tsym_us) / 4;

  // airtime for max packet at current radio settings
  uint32_t total_us   = _radio->getTimeOnAir(MAX_TRANS_UNIT);
  // airtime for payload only (no preamble, header or SOF)
  uint32_t payload_us = total_us > preamble_us ? total_us - preamble_us : 4000 - preamble_us; // fallback to 4 secs at worst case
  // rescale payload_us for max possible CR
  if (cr >= 5 && cr < 8) { payload_us = (payload_us * 8) / cr; }

  return PacketMillis {(preamble_us + 999) / 1000, (payload_us + 999) / 1000};
}