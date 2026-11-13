#pragma once

// =============================================================================
// WatchVoiceScreen -- voice notes for the LilyGo T-Watch S3, SX1280 builds only
// (MECK_WATCH_VOICE). Ported from Meck's VoiceMessageScreen.h (T-Deck).
// Receives the dz0ny VE3 format (envelope DM, raw 0x56 voice packets of up to
// 150 bytes of Codec2 1200 data, at most 16 packets / 12 seconds per note).
// Sends by flood instead (E, Nov 2026; see the WV_MSG_* table): to the voice
// channel, or to a contact along its direct path first, then by private flood.
//
// Differences from the T-Deck screen, as decided by E (Nov 2026):
//   - Recording starts and stops on a short press of the PWR key (KEY_ENTER),
//     stopping by itself at 12 seconds.
//   - Notes are stored as Codec2 data (not WAV) on the LittleFS "maps"
//     partition under /voice, and decoded when played.
//   - Incoming notes are not auto-played: main.cpp shows a toast and the note
//     appears in the list marked new; opening it plays it.
//   - The CPU runs at 240 MHz only while encoding or decoding, then returns
//     to where it was (80 MHz on these builds).
//   - Audio is filtered (version B of the Nov 2026 audio test): DC removal,
//     normalise to 90% of full scale, 150 Hz hum high-pass, then a 31-tap
//     3.6 kHz low-pass before 16 -> 8 kHz; the same low-pass smooths
//     8 -> 16 kHz on playback. The Codec2 data itself is plain 1200 bps.
//
// Screens: LIST, RECORDING, ENCODING, VIEW (open note: play / send / delete),
// PICK (send-to contact list). Geometry is 120x120 logical (UI_ZOOM=2), as in
// WatchNotesScreen. The top status strip is handled by main.cpp (-> home).
//
// Include order: after UITask.h (needs NodePrefs), as WatchNotesScreen.h.
// =============================================================================

#if defined(MECK_WATCH_VOICE)

#include <Arduino.h>
#include <string.h>
#include <math.h>
#include <LittleFS.h>
#include <esp_timer.h>
#include <MeshCore.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/LGFXDisplay.h>
#include <codec2.h>
#include "WatchVoiceAudio.h"

class UITask;

// ---- VE3 protocol (must match Meck's VoiceMessageScreen.h and Meck-P4) ----
#define WV_PKT_MAGIC        0x56   // 'V' voice data packet
#define WV_FETCH_MAGIC      0x72   // 'r' fetch request
#define WV_PKT_HDR_SIZE     6      // magic(1) + sessionId(4) + index(1)
#define WV_MODE_ID          1      // Codec2 1200 in the VE3 envelope
#define WV_MESH_PAYLOAD     150    // Codec2 bytes per voice packet
#define WV_MAX_PACKETS      16     // receivers track packets in a 16-bit map
#define WV_SESSION_TTL_MS   900000 // outgoing session kept 15 min for fetches

// ---- Flood modes (Meck only; channel data or contact requests, Nov 2026) ----
// Every message starts WV_PKT_MAGIC, then one of these:
//   'H' header  [V][H][sid:4][total][dur][sender name...]   (channel only)
//   'D' data    [V][D][sid:4][idx][total][len][Codec2 x len]
//   '?' status  [V][?][sid:4]                                (contact only)
//   'R' report  [V][R][sid:4][received bitmap:2 LE][total]   (contact's reply)
// idx | WV_FLAG_REPORT on a contact data packet asks for a report back.
#define WV_MSG_HEADER       'H'
#define WV_MSG_DATA         'D'
#define WV_MSG_STATUS       '?'
#define WV_MSG_REPORT       'R'
#define WV_FLAG_REPORT      0x80
#define WV_DATA_HDR_SIZE    9      // V D sid(4) idx total len
#define WV_REPORT_SIZE      9      // V R sid(4) bitmap(2) total

// ---- Codec2 1200 ----
#define WV_MAX_SECONDS      12
#define WV_FRAME_MS         40
#define WV_FRAME_SAMPLES    320    // 8 kHz x 40 ms
#define WV_FRAME_BYTES      6      // 48 bits
#define WV_MAX_FRAMES       (WV_MAX_SECONDS * 1000 / WV_FRAME_MS)   // 300
#define WV_MAX_BYTES        (WV_MAX_FRAMES * WV_FRAME_BYTES)        // 1800

// ---- Audio buffers ----
#define WV_RATE             16000
#define WV_MAX_SAMPLES      (WV_RATE * WV_MAX_SECONDS)              // 192000
#define WV_MAX_SAMPLES_8K   (WV_MAX_SAMPLES / 2)                    // 96000

// ---- Filters (audio test version B) ----
#define WV_HPF_HZ           150.0f
#define WV_LPF_HZ           3600.0f
#define WV_FIR_TAPS         31

// ---- Storage ----
#define WV_DIR              "/voice"
#define WV_LIST_MAX         24
#define WV_NAME_LEN         24     // "YYMMDD-HHMMSS-9.vn" + NUL
#define WV_LABEL_LEN        20
#define WV_HDR_SIZE         48

// ---- CPU ----
#define WV_CPU_FAST         240
#define WV_STOP_TRIM_MS     400    // dropped from the end when PWR stops recording
#define WV_LEVEL_PERMILLE   999    // normalise so 99.9% of samples sit below 90%

class WatchVoiceScreen : public UIScreen {
public:
  enum Mode { LIST, RECORDING, ENCODING, VIEW, PICK };
  enum Kind : uint8_t { KIND_RECORDED = 0, KIND_RECEIVED = 1, KIND_SENT = 2,
                        KIND_CH_RECEIVED = 3 };

  // Send-to entry, filled by main.cpp: the voice channel first (meshIdx =
  // PICK_CHANNEL), then contacts.
  static const int PICK_CHANNEL = -2;
  struct PickContact {
    int     meshIdx;
    char    name[32];
    bool    hasDirect;
  };

private:
  UITask*          _task;
  mesh::RTCClock*  _rtc;
  NodePrefs*       _node_prefs;
  WatchVoiceAudio  _audio;

  Mode _mode = LIST;

  // ---- List ----
  int  _count = 0;
  int  _sel = 0;
  int  _scroll = 0;
  char    _names[WV_LIST_MAX][WV_NAME_LEN];
  char    _labels[WV_LIST_MAX][WV_LABEL_LEN];
  uint8_t _kinds[WV_LIST_MAX];
  uint8_t _unread[WV_LIST_MAX];
  uint8_t _durs[WV_LIST_MAX];

  // ---- Open note ----
  char     _curName[WV_NAME_LEN];
  char     _curLabel[32];
  uint8_t  _curKind = KIND_RECORDED;
  uint8_t  _curDur = 0;
  uint8_t  _c2[WV_MAX_BYTES];
  uint32_t _c2Bytes = 0;
  bool     _delArmed = false;

  // ---- PCM buffers (PSRAM, allocated on demand, freed by releaseBuffers) ----
  int16_t* _pcm = nullptr;      // 16 kHz: recording, then playback
  int16_t* _pcm8k = nullptr;    // 8 kHz: encoder input / decoder output
  uint32_t _pcmSamples = 0;     // valid samples in _pcm
  bool     _pcmIsCur = false;   // _pcm holds the decoded audio of _curName

  // ---- Recording ----
  unsigned long _recStart = 0;

  // ---- Encode / decode (chunked across polls) ----
  struct CODEC2* _codec = nullptr;
  uint32_t _workPos = 0;        // next 8 kHz sample (encode) / next byte (decode)
  uint32_t _workEnd = 0;
  bool     _decoding = false;   // VIEW: decoding before playback
  int      _prevMhz = 0;

  // ---- Playback ----
  bool     _playing = false;
  uint32_t _playPos = 0;
  unsigned long _drainUntil = 0;   // all queued; stop the amp after this

  // ---- Picker ----
  PickContact _pick[40];
  int  _pickCount = 0;
  int  _pickSel = 0;
  int  _pickScroll = 0;
  bool _pickLoaded = false;
  int  _pendingSendIdx = -1;
  char _pendingSendName[32];

  // ---- FIR taps ----
  float _fir[WV_FIR_TAPS];

  // ---- VE3 outgoing session (served to fetch requests) ----
  struct OutSession {
    uint32_t sessionId;
    uint8_t  data[WV_MAX_BYTES];
    uint32_t dataBytes;
    uint8_t  totalPackets;
    unsigned long cachedAt;
    bool     active;
  } _out;

  // ---- VE3 incoming session ----
  struct InSession {
    uint32_t sessionId;
    uint8_t  data[WV_MAX_BYTES];
    uint16_t pktOffset[WV_MAX_PACKETS];
    uint16_t pktSize[WV_MAX_PACKETS];
    uint8_t  totalPackets;
    uint16_t receivedBitmap;
    uint8_t  receivedCount;
    uint32_t dataBytes;
    uint8_t  durationSec;
    char     senderName[32];
    unsigned long startedAt;
    bool     active;
    bool     complete;
    bool     saved;
    bool     viaChannel;           // arrived on the voice channel
  } _in;

  // Last note saved: late or repeated packets of it are ignored, and a status
  // request for it is still answered (all received).
  uint32_t _lastSavedSid = 0;
  uint16_t _lastSavedBitmap = 0;
  uint8_t  _lastSavedTotal = 0;

  // ===========================================================================
  // Helpers
  // ===========================================================================
  int32_t localNow() const {
    uint32_t utc = _rtc->getCurrentTime();
    if (utc < 1700000000) return -1;
    return (int32_t)utc + ((int32_t)_node_prefs->utc_offset_hours * 3600);
  }

  static void civilFromDays(int32_t z, int& y, int& m, int& d) {
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t yr = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yr + (m <= 2));
  }

  // New file name, YYMMDD-HHMMSS.vn local time (u<millis>.vn without a clock),
  // with -1..-9 added if that name is already taken.
  void makeName(char* out, size_t outLen) {
    char base[20];
    int32_t local = localNow();
    if (local < 0) {
      snprintf(base, sizeof(base), "u%07lu", (unsigned long)(millis() % 10000000UL));
    } else {
      int y, mo, d;
      civilFromDays(local / 86400, y, mo, d);
      snprintf(base, sizeof(base), "%02d%02d%02d-%02d%02d%02d", y % 100, mo, d,
               (int)((local / 3600) % 24), (int)((local / 60) % 60), (int)(local % 60));
    }
    snprintf(out, outLen, "%s.vn", base);
    char path[48];
    for (int n = 1; n <= 9; n++) {
      snprintf(path, sizeof(path), WV_DIR "/%s", out);
      if (!LittleFS.exists(path)) return;
      snprintf(out, outLen, "%s-%d.vn", base, n);
    }
  }

  // "260714-173245.vn" -> "14/07 17:32"; u-names -> "no clock"
  static void dateLabel(const char* name, char* out, size_t outLen) {
    if (name[0] == 'u' || strlen(name) < 13) { snprintf(out, outLen, "no clock"); return; }
    snprintf(out, outLen, "%.2s/%.2s %.2s:%.2s", name + 4, name + 2, name + 7, name + 9);
  }

  static const char* leafOf(const char* n) {
    const char* s = strrchr(n, '/');
    return s ? s + 1 : n;
  }

  static void kindLabel(uint8_t kind, const char* who, char* out, size_t outLen) {
    if (kind == KIND_RECEIVED)  snprintf(out, outLen, "From %s", who);
    else if (kind == KIND_CH_RECEIVED) snprintf(out, outLen, "Ch %s", who);
    else if (kind == KIND_SENT) snprintf(out, outLen, "To %s", who);
    else                        snprintf(out, outLen, "Recorded");
  }

  // ---- CPU: 240 MHz only while encoding or decoding ----
  void cpuFast() {
    if (_prevMhz) return;
    _prevMhz = getCpuFrequencyMhz();
    if (_prevMhz < WV_CPU_FAST) setCpuFrequencyMhz(WV_CPU_FAST);
  }
  void cpuRestore() {
    if (!_prevMhz) return;
    if (getCpuFrequencyMhz() != _prevMhz) setCpuFrequencyMhz(_prevMhz);
    _prevMhz = 0;
  }

  // ---- PSRAM buffers ----
  bool ensureBuffers() {
    if (!_pcm)   _pcm   = (int16_t*)ps_calloc(WV_MAX_SAMPLES, sizeof(int16_t));
    if (!_pcm8k) _pcm8k = (int16_t*)ps_calloc(WV_MAX_SAMPLES_8K, sizeof(int16_t));
    if (!_pcm || !_pcm8k) {
      Serial.println("Voice: PSRAM buffer allocation failed");
      releaseBuffers();
      return false;
    }
    return true;
  }
  void releaseBuffers() {
    if (_pcm)   { free(_pcm);   _pcm = nullptr; }
    if (_pcm8k) { free(_pcm8k); _pcm8k = nullptr; }
    _pcmSamples = 0;
    _pcmIsCur = false;
  }

  // ---- Filters (identical to audio test version B) ----
  void firDesign() {
    const int M = (WV_FIR_TAPS - 1) / 2;
    const float fc = WV_LPF_HZ / WV_RATE;
    float sum = 0;
    for (int n = 0; n < WV_FIR_TAPS; n++) {
      int k = n - M;
      float sinc = (k == 0) ? 2.0f * fc : sinf(2.0f * PI * fc * k) / (PI * k);
      float win = 0.54f - 0.46f * cosf(2.0f * PI * n / (WV_FIR_TAPS - 1));
      _fir[n] = sinc * win;
      sum += _fir[n];
    }
    for (int n = 0; n < WV_FIR_TAPS; n++) _fir[n] /= sum;
  }

  static inline int16_t clip16(float v) {
    if (v > 32767.0f) return 32767;
    if (v < -32768.0f) return -32768;
    return (int16_t)v;
  }

  // _pcm (16 kHz, _pcmSamples) -> DC removal + normalise (in place), then
  // high-pass (in place), then low-pass + decimate into _pcm8k. Returns the
  // number of 8 kHz samples.
  uint32_t prepareForEncode() {
    uint32_t n = _pcmSamples;
    if (n == 0) return 0;
    int64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) sum += _pcm[i];
    int32_t dc = (int32_t)(sum / (int64_t)n);
    // Level = the loudness that 99.9% of samples stay under (histogram of
    // 256 steps of 128), so one click or knock can't hold the gain down.
    static uint32_t hist[256];
    memset(hist, 0, sizeof(hist));
    int32_t peak = 0;
    for (uint32_t i = 0; i < n; i++) {
      int32_t v = _pcm[i] - dc;
      if (v > 32767) v = 32767;
      if (v < -32768) v = -32768;
      _pcm[i] = (int16_t)v;
      int32_t a = v < 0 ? -v : v;
      if (a > peak) peak = a;
      hist[a >> 7 > 255 ? 255 : a >> 7]++;
    }
    uint64_t want = (uint64_t)n * WV_LEVEL_PERMILLE / 1000;
    uint64_t seen = 0;
    int32_t level = peak;
    for (int b = 0; b < 256; b++) {
      seen += hist[b];
      if (seen >= want) { level = (b + 1) << 7; break; }
    }
    if (level > peak) level = peak;
    int32_t gain16 = 1 << 16;
    if (level >= 100) {
      gain16 = (int32_t)(((int64_t)29491 << 16) / level);   // 90% of full scale
      for (uint32_t i = 0; i < n; i++) {
        int32_t v = (int32_t)(((int64_t)_pcm[i] * gain16) >> 16);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        _pcm[i] = (int16_t)v;
      }
    }
    Serial.printf("Voice: %lu samples, dc %ld, peak %ld, level %ld, gain %.1fx\n",
                  (unsigned long)n, (long)dc, (long)peak, (long)level, gain16 / 65536.0f);

    // 2nd-order Butterworth high-pass (RBJ biquad)
    const float w0 = 2.0f * PI * WV_HPF_HZ / WV_RATE;
    const float alpha = sinf(w0) / (2.0f * 0.7071f);
    const float cw = cosf(w0);
    const float a0 = 1.0f + alpha;
    const float b0 = ((1.0f + cw) / 2.0f) / a0, b1 = (-(1.0f + cw)) / a0, b2 = b0;
    const float a1 = (-2.0f * cw) / a0, a2 = (1.0f - alpha) / a0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (uint32_t i = 0; i < n; i++) {
      float x = _pcm[i];
      float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1; x1 = x; y2 = y1; y1 = y;
      _pcm[i] = clip16(y);
    }

    const int M = (WV_FIR_TAPS - 1) / 2;
    uint32_t n8 = n / 2;
    for (uint32_t k = 0; k < n8; k++) {
      int32_t centre = (int32_t)(k * 2);
      float acc = 0;
      for (int t = 0; t < WV_FIR_TAPS; t++) {
        int32_t idx = centre + M - t;
        if (idx >= 0 && idx < (int32_t)n) acc += _fir[t] * _pcm[idx];
      }
      _pcm8k[k] = clip16(acc);
    }
    return n8;
  }

  // _pcm8k (n8 samples) -> _pcm at 16 kHz: zero-stuff + the same low-pass, x2.
  void upsampleForPlay(uint32_t n8) {
    const int M = (WV_FIR_TAPS - 1) / 2;
    uint32_t n16 = n8 * 2;
    for (uint32_t m = 0; m < n16; m++) {
      float acc = 0;
      for (int t = 0; t < WV_FIR_TAPS; t++) {
        int32_t idx = (int32_t)m + M - t;
        if (idx >= 0 && idx < (int32_t)n16 && (idx & 1) == 0) acc += _fir[t] * _pcm8k[idx / 2];
      }
      _pcm[m] = clip16(2.0f * acc);
    }
    _pcmSamples = n16;
  }

  // ===========================================================================
  // Storage. File = 48-byte header + Codec2 bytes:
  //   0 "MVN1"  4 kind  5 unread  6 VE3 mode id  7 duration (s)
  //   8 uint32 UTC time  12 char[32] sender/recipient  44 uint32 data bytes
  // ===========================================================================
  bool writeNote(const char* leaf, uint8_t kind, uint8_t unread, uint8_t dur,
                 const char* who, const uint8_t* data, uint32_t len) {
    if (!LittleFS.exists(WV_DIR)) LittleFS.mkdir(WV_DIR);
    char path[48];
    snprintf(path, sizeof(path), WV_DIR "/%s", leaf);
    File f = LittleFS.open(path, "w");
    if (!f) return false;
    uint8_t h[WV_HDR_SIZE];
    memset(h, 0, sizeof(h));
    memcpy(h, "MVN1", 4);
    h[4] = kind;
    h[5] = unread;
    h[6] = WV_MODE_ID;
    h[7] = dur;
    uint32_t ts = _rtc->getCurrentTime();
    memcpy(&h[8], &ts, 4);
    if (who) strncpy((char*)&h[12], who, 31);
    memcpy(&h[44], &len, 4);
    bool ok = f.write(h, sizeof(h)) == sizeof(h) && f.write(data, len) == len;
    f.close();
    return ok;
  }

  bool readHeader(const char* leaf, uint8_t* h) {
    char path[48];
    snprintf(path, sizeof(path), WV_DIR "/%s", leaf);
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    bool ok = f.read(h, WV_HDR_SIZE) == WV_HDR_SIZE && memcmp(h, "MVN1", 4) == 0;
    f.close();
    return ok;
  }

  // Rewrites one header byte (unread flag) or the kind + name in place.
  void patchHeader(const char* leaf, int offset, const uint8_t* bytes, int len) {
    char path[48];
    snprintf(path, sizeof(path), WV_DIR "/%s", leaf);
    File f = LittleFS.open(path, "r+");
    if (!f) return;
    if (f.seek(offset)) f.write(bytes, len);
    f.close();
  }

  void scanNotes() {
    _count = 0;
    if (!LittleFS.exists(WV_DIR)) LittleFS.mkdir(WV_DIR);
    File root = LittleFS.open(WV_DIR);
    if (!root || !root.isDirectory()) return;
    File f = root.openNextFile();
    while (f && _count < WV_LIST_MAX) {
      if (!f.isDirectory()) {
        const char* leaf = leafOf(f.name());
        size_t n = strlen(leaf);
        if (n > 3 && n < WV_NAME_LEN && strcmp(leaf + n - 3, ".vn") == 0) {
          int pos = _count;   // insertion sort, descending name = newest first
          while (pos > 0 && strcmp(leaf, _names[pos - 1]) > 0) pos--;
          for (int k = _count; k > pos; k--) memcpy(_names[k], _names[k - 1], WV_NAME_LEN);
          strncpy(_names[pos], leaf, WV_NAME_LEN - 1);
          _names[pos][WV_NAME_LEN - 1] = 0;
          _count++;
        }
      }
      f = root.openNextFile();
    }
    root.close();
    for (int i = 0; i < _count; i++) {
      uint8_t h[WV_HDR_SIZE];
      if (readHeader(_names[i], h)) {
        _kinds[i] = h[4];
        _unread[i] = h[5];
        _durs[i] = h[7];
        char who[32];
        memcpy(who, &h[12], 31);
        who[31] = 0;
        kindLabel(h[4], who, _labels[i], WV_LABEL_LEN);
      } else {
        _kinds[i] = KIND_RECORDED; _unread[i] = 0; _durs[i] = 0;
        snprintf(_labels[i], WV_LABEL_LEN, "(unreadable)");
      }
    }
    if (_sel >= _count) _sel = _count ? _count - 1 : 0;
    if (_scroll > _sel) _scroll = _sel;
  }

  // Loads a note's header + Codec2 data into the open-note state.
  bool loadNote(const char* leaf) {
    char path[48];
    snprintf(path, sizeof(path), WV_DIR "/%s", leaf);
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    uint8_t h[WV_HDR_SIZE];
    bool ok = f.read(h, WV_HDR_SIZE) == WV_HDR_SIZE && memcmp(h, "MVN1", 4) == 0;
    uint32_t len = 0;
    if (ok) {
      memcpy(&len, &h[44], 4);
      if (len > WV_MAX_BYTES) len = WV_MAX_BYTES;
      ok = f.read(_c2, len) == (int)len;
    }
    f.close();
    if (!ok) return false;
    _c2Bytes = len;
    _curKind = h[4];
    _curDur = h[7];
    char who[32];
    memcpy(who, &h[12], 31);
    who[31] = 0;
    kindLabel(_curKind, who, _curLabel, sizeof(_curLabel));
    strncpy(_curName, leaf, WV_NAME_LEN - 1);
    _curName[WV_NAME_LEN - 1] = 0;
    _pcmIsCur = false;
    if (h[5]) {                        // opening marks it read
      uint8_t zero = 0;
      patchHeader(leaf, 5, &zero, 1);
    }
    return true;
  }

  // ===========================================================================
  // Recording -> encode
  // ===========================================================================
  void startRecording() {
    stopPlayback();
    if (!ensureBuffers()) { _mode = LIST; return; }
    _pcmIsCur = false;
    if (!_audio.micStart()) { _mode = LIST; return; }
    // Discard ~250 ms of PDM start-up output (as in the audio test).
    unsigned long until = millis() + 250;
    while (millis() < until) { _audio.micRead(_pcm, 1024); delay(5); }
    _pcmSamples = 0;
    _recStart = millis();
    _mode = RECORDING;
    Serial.println("Voice: recording");
  }

  // byKey: stopped with the PWR key. The press itself is picked up by the mic,
  // so the last WV_STOP_TRIM_MS of the recording is dropped.
  void stopRecording(bool byKey) {
    _pcmSamples += _audio.micRead(&_pcm[_pcmSamples], WV_MAX_SAMPLES - _pcmSamples);
    _audio.micStop();
    uint32_t trim = 0;
    if (byKey) {
      trim = (uint32_t)WV_RATE * WV_STOP_TRIM_MS / 1000;
      if (trim > _pcmSamples) trim = _pcmSamples;
      _pcmSamples -= trim;
    }
    Serial.printf("Voice: recorded %lu samples in %lu ms (%lu trimmed)\n",
                  (unsigned long)_pcmSamples, millis() - _recStart, (unsigned long)trim);
    if (_pcmSamples < WV_RATE / 2) {   // under half a second: nothing to keep
      _mode = LIST;
      return;
    }
    _mode = ENCODING;
    _workPos = 0;
    _workEnd = 0;
    _c2Bytes = 0;
  }

  // One step of encoding. Called from poll(); returns when done or after
  // ~40 ms so the mesh loop keeps running.
  void encodeStep() {
    if (_workEnd == 0) {               // first step: filter, open the encoder
      cpuFast();
      _workEnd = prepareForEncode();
      _workEnd -= _workEnd % WV_FRAME_SAMPLES;
      _codec = codec2_create(CODEC2_MODE_1200);
      if (!_codec || _workEnd == 0) { finishEncode(false); return; }
      return;
    }
    int64_t t0 = esp_timer_get_time();
    while (_workPos + WV_FRAME_SAMPLES <= _workEnd && _c2Bytes + WV_FRAME_BYTES <= WV_MAX_BYTES) {
      codec2_encode(_codec, &_c2[_c2Bytes], &_pcm8k[_workPos]);
      _workPos += WV_FRAME_SAMPLES;
      _c2Bytes += WV_FRAME_BYTES;
      if (esp_timer_get_time() - t0 > 40000) return;
    }
    finishEncode(true);
  }

  void finishEncode(bool ok) {
    if (_codec) { codec2_destroy(_codec); _codec = nullptr; }
    cpuRestore();
    if (!ok || _c2Bytes == 0) {
      Serial.println("Voice: encode failed");
      _mode = LIST;
      return;
    }
    uint8_t dur = (uint8_t)((_c2Bytes / WV_FRAME_BYTES) * WV_FRAME_MS / 1000);
    char leaf[WV_NAME_LEN];
    makeName(leaf, sizeof(leaf));
    bool saved = writeNote(leaf, KIND_RECORDED, 0, dur, "", _c2, _c2Bytes);
    Serial.printf("Voice: encoded %lu bytes (%us), saved %s: %s\n",
                  (unsigned long)_c2Bytes, dur, leaf, saved ? "OK" : "FAILED");
    scanNotes();
    if (saved && loadNote(leaf)) {
      _delArmed = false;
      _mode = VIEW;                    // review: Play / Send / Del
    } else {
      _mode = LIST;
    }
  }

  // ===========================================================================
  // Decode -> playback
  // ===========================================================================
  void startPlayback() {
    if (_c2Bytes == 0) return;
    if (_pcmIsCur && _pcm) {           // already decoded: just play again
      beginOutput();
      return;
    }
    if (!ensureBuffers()) return;
    _codec = codec2_create(CODEC2_MODE_1200);
    if (!_codec) return;
    cpuFast();
    _workPos = 0;
    _workEnd = _c2Bytes - (_c2Bytes % WV_FRAME_BYTES);
    _pcmSamples = 0;
    _decoding = true;
  }

  void decodeStep() {
    int64_t t0 = esp_timer_get_time();
    uint32_t out8 = (_workPos / WV_FRAME_BYTES) * WV_FRAME_SAMPLES;
    while (_workPos + WV_FRAME_BYTES <= _workEnd && out8 + WV_FRAME_SAMPLES <= WV_MAX_SAMPLES_8K) {
      codec2_decode(_codec, &_pcm8k[out8], &_c2[_workPos]);
      _workPos += WV_FRAME_BYTES;
      out8 += WV_FRAME_SAMPLES;
      if (esp_timer_get_time() - t0 > 40000) return;
    }
    codec2_destroy(_codec);
    _codec = nullptr;
    upsampleForPlay(out8);
    cpuRestore();
    _decoding = false;
    _pcmIsCur = true;
    beginOutput();
  }

  void beginOutput() {
    if (!_audio.spkStart()) return;
    _playPos = 0;
    _drainUntil = 0;
    _playing = true;
  }

  void playStep() {
    if (_playPos < _pcmSamples) {
      _playPos += _audio.spkWrite(&_pcm[_playPos], _pcmSamples - _playPos);
      if (_playPos >= _pcmSamples) _drainUntil = millis() + 600;   // ~0.5 s queued
      return;
    }
    if (_drainUntil && millis() >= _drainUntil) stopPlayback();
  }

  void stopPlayback() {
    if (_decoding) {
      if (_codec) { codec2_destroy(_codec); _codec = nullptr; }
      cpuRestore();
      _decoding = false;
    }
    if (_playing || _audio.spkOn()) _audio.spkStop();
    _playing = false;
  }

  void deleteCurrent() {
    stopPlayback();
    char path[48];
    snprintf(path, sizeof(path), WV_DIR "/%s", _curName);
    LittleFS.remove(path);
    _delArmed = false;
    _pcmIsCur = false;
    _mode = LIST;
    scanNotes();
  }

  void openSelected() {
    if (_sel < 0 || _sel >= _count) return;
    stopPlayback();
    if (!loadNote(_names[_sel])) return;
    _unread[_sel] = 0;
    _delArmed = false;
    _mode = VIEW;
    startPlayback();                   // opening a note plays it
  }

  // ===========================================================================
  // Rendering (120x120 logical)
  // ===========================================================================
  static const int ROW_Y0 = 18;
  static const int ROW_H  = 20;
  static const int ROWS_VISIBLE = 4;
  static const int FOOTER_Y = 104;

  void ensureVisible() {
    if (_sel < _scroll) _scroll = _sel;
    if (_sel >= _scroll + ROWS_VISIBLE) _scroll = _sel - ROWS_VISIBLE + 1;
  }

  void drawScrollbar(DisplayDriver& display, int count, int scroll) {
    if (count <= ROWS_VISIBLE) return;
    int sbX = display.width() - 4;
    int sbTop = ROW_Y0;
    int sbH = FOOTER_Y - 4 - sbTop;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(sbX, sbTop, 3, sbH);
    int thumbH = (ROWS_VISIBLE * sbH) / count;
    if (thumbH < 6) thumbH = 6;
    if (thumbH > sbH) thumbH = sbH;
    int maxScroll = count - ROWS_VISIBLE;
    if (maxScroll < 1) maxScroll = 1;
    int thumbY = sbTop + (scroll * (sbH - thumbH)) / maxScroll;
    display.fillRect(sbX + 1, thumbY + 1, 1, thumbH - 2);
  }

  void renderList(DisplayDriver& display) {
    LGFXDisplay* d = (LGFXDisplay*)&display;
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 6, "Voice");

    if (_count == 0) {
      d->setRawColor(0x7BEF);
      display.drawTextCentered(display.width() / 2, 50, "No voice notes");
    }

    char buf[24];
    for (int i = 0; i < ROWS_VISIBLE; i++) {
      int idx = _scroll + i;
      if (idx >= _count) break;
      int y = ROW_Y0 + i * ROW_H;
      if (idx == _sel) {
        d->setRawColor(0xFD20);                      // Voice tile orange
        d->drawRoundRect(1, y, display.width() - 2 - (_count > ROWS_VISIBLE ? 5 : 0), ROW_H - 2, 3);
      }
      int textX = 5;
      if (_unread[idx]) {                            // new-note marker
        d->setRawColor(0x07E0);
        d->fillRoundRect(4, y + 4, 5, 5, 2);
        textX = 12;
      }
      d->setRawColor(0xFFFF);
      display.setCursor(textX, y + 2);
      display.print(_labels[idx]);

      char date[16];
      dateLabel(_names[idx], date, sizeof(date));
      snprintf(buf, sizeof(buf), "%s  %us", date, (unsigned)_durs[idx]);
      d->setRawColor(0x7BEF);
      d->printSmallFont(5, y + 13, buf);
    }
    drawScrollbar(display, _count, _scroll);

    d->setRawColor(0xC618);
    display.drawTextCentered(display.width() / 2, FOOTER_Y + 4, "PWR: record");
  }

  void renderRecording(DisplayDriver& display) {
    LGFXDisplay* d = (LGFXDisplay*)&display;
    unsigned long ms = millis() - _recStart;
    if (ms > WV_MAX_SECONDS * 1000UL) ms = WV_MAX_SECONDS * 1000UL;
    d->setRawColor(0xF800);
    d->fillRoundRect(display.width() / 2 - 6, 26, 12, 12, 6);
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 46, "Recording");
    char buf[16];
    snprintf(buf, sizeof(buf), "%lus / %ds", ms / 1000, WV_MAX_SECONDS);
    display.drawTextCentered(display.width() / 2, 60, buf);
    int barW = display.width() - 20;
    d->setRawColor(0x7BEF);
    d->drawRoundRect(10, 74, barW, 6, 2);
    d->setRawColor(0xF800);
    d->fillRect(11, 75, (int)((barW - 2) * ms / (WV_MAX_SECONDS * 1000UL)), 4);
    d->setRawColor(0xC618);
    display.drawTextCentered(display.width() / 2, FOOTER_Y + 4, "PWR: stop");
  }

  void renderEncoding(DisplayDriver& display) {
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    char buf[16];
    int pct = (_workEnd > 0) ? (int)(_workPos * 100 / _workEnd) : 0;
    snprintf(buf, sizeof(buf), "Saving %d%%", pct);
    display.drawTextCentered(display.width() / 2, 54, buf);
  }

  void renderView(DisplayDriver& display) {
    LGFXDisplay* d = (LGFXDisplay*)&display;
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 6, _curLabel);
    char date[16], buf[24];
    dateLabel(_curName, date, sizeof(date));
    snprintf(buf, sizeof(buf), "%s  %us", date, (unsigned)_curDur);
    d->setRawColor(0x7BEF);
    d->printSmallFont((display.width() - d->smallTextWidth(buf)) / 2, 22, buf);

    // Centre: play state; tapping this area plays / stops.
    display.setColor(DisplayDriver::LIGHT);
    if (_decoding) {
      int pct = (_workEnd > 0) ? (int)(_workPos * 100 / _workEnd) : 0;
      snprintf(buf, sizeof(buf), "Loading %d%%", pct);
      display.drawTextCentered(display.width() / 2, 52, buf);
    } else if (_playing) {
      uint32_t pos = _playPos > _pcmSamples ? _pcmSamples : _playPos;
      snprintf(buf, sizeof(buf), "Playing %lus", (unsigned long)(pos / WV_RATE));
      display.drawTextCentered(display.width() / 2, 52, buf);
      int barW = display.width() - 20;
      d->setRawColor(0x7BEF);
      d->drawRoundRect(10, 66, barW, 6, 2);
      d->setRawColor(0x07E0);
      if (_pcmSamples) d->fillRect(11, 67, (int)((barW - 2) * (uint64_t)pos / _pcmSamples), 4);
    } else {
      d->setRawColor(0x07E0);
      display.drawTextCentered(display.width() / 2, 52, "Tap to play");
    }

    // Footer: [Back] [Send] [Del]
    d->setRawColor(0x231D);
    d->drawRoundRect(1, FOOTER_Y, 38, 14, 3);
    d->drawRoundRect(41, FOOTER_Y, 38, 14, 3);
    d->setRawColor(_delArmed ? 0xF800 : 0x231D);
    d->drawRoundRect(81, FOOTER_Y, 38, 14, 3);
    d->setRawColor(0xC618);
    d->printSmallFont(9, FOOTER_Y + 8, "Back");
    d->printSmallFont(49, FOOTER_Y + 8, "Send");
    d->setRawColor(_delArmed ? 0xF800 : 0xC618);
    d->printSmallFont(87, FOOTER_Y + 8, _delArmed ? "Sure?" : "Del");
  }

  void renderPick(DisplayDriver& display) {
    LGFXDisplay* d = (LGFXDisplay*)&display;
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 6, "Send to");

    if (!_pickLoaded) {
      d->setRawColor(0x7BEF);
      display.drawTextCentered(display.width() / 2, 50, "Loading...");
    } else if (_pickCount == 0) {
      d->setRawColor(0x7BEF);
      display.drawTextCentered(display.width() / 2, 50, "No contacts");
    }
    for (int i = 0; i < ROWS_VISIBLE && _pickLoaded; i++) {
      int idx = _pickScroll + i;
      if (idx >= _pickCount) break;
      int y = ROW_Y0 + i * ROW_H;
      if (idx == _pickSel) {
        d->setRawColor(0xFD20);
        d->drawRoundRect(1, y, display.width() - 2 - (_pickCount > ROWS_VISIBLE ? 5 : 0), ROW_H - 2, 3);
      }
      bool isCh = (_pick[idx].meshIdx == PICK_CHANNEL);
      d->setRawColor(isCh ? 0xFD20 : 0xFFFF);
      display.setCursor(5, y + 2);
      display.print(_pick[idx].name);
      d->setRawColor(0x7BEF);
      d->printSmallFont(5, y + 13, isCh ? "channel" : (_pick[idx].hasDirect ? "direct" : "flood"));
    }
    drawScrollbar(display, _pickCount, _pickScroll);

    d->setRawColor(0x231D);
    d->drawRoundRect(31, FOOTER_Y, 58, 14, 3);
    d->setRawColor(0xC618);
    d->printSmallFont(50, FOOTER_Y + 8, "Back");
  }

public:
  WatchVoiceScreen(UITask* task, mesh::RTCClock* rtc, NodePrefs* node_prefs)
    : _task(task), _rtc(rtc), _node_prefs(node_prefs) {
    _curName[0] = 0;
    _curLabel[0] = 0;
    _pendingSendName[0] = 0;
    memset(&_out, 0, sizeof(_out));
    memset(&_in, 0, sizeof(_in));
    firDesign();
  }

  void enter() {
    _mode = LIST;
    _delArmed = false;
    scanNotes();
    _sel = 0;
    _scroll = 0;
  }

  Mode getMode() const { return _mode; }

  // True while the screen needs the loop serviced promptly and the display
  // kept on (recording, encoding, decoding or playing).
  bool isBusy() const {
    return _mode == RECORDING || _mode == ENCODING || _decoding || _playing;
  }

  // Called by main.cpp whenever the voice screen is not the current screen:
  // anything in progress is stopped and the PSRAM buffers are released.
  void leave() {
    if (_mode == RECORDING) { _audio.micStop(); _mode = LIST; }
    if (_mode == ENCODING) {
      if (_codec) { codec2_destroy(_codec); _codec = nullptr; }
      cpuRestore();
      _mode = LIST;
    }
    stopPlayback();
    if (_mode == PICK) _mode = VIEW;
    releaseBuffers();
  }

  // Watch vertical swipe: page the list / picker by one screenful.
  void pageScroll(bool up) {
    if (_mode == LIST && _count > ROWS_VISIBLE) {
      int maxScroll = _count - ROWS_VISIBLE;
      _scroll += up ? -ROWS_VISIBLE : ROWS_VISIBLE;
      if (_scroll < 0) _scroll = 0;
      if (_scroll > maxScroll) _scroll = maxScroll;
    } else if (_mode == PICK && _pickCount > ROWS_VISIBLE) {
      int maxScroll = _pickCount - ROWS_VISIBLE;
      _pickScroll += up ? -ROWS_VISIBLE : ROWS_VISIBLE;
      if (_pickScroll < 0) _pickScroll = 0;
      if (_pickScroll > maxScroll) _pickScroll = maxScroll;
    }
  }

  // ---- Picker handshake with main.cpp ----
  bool needsPickContacts() const { return _mode == PICK && !_pickLoaded; }
  void loadPickContacts(const PickContact* list, int count) {
    if (count > 40) count = 40;
    for (int i = 0; i < count; i++) _pick[i] = list[i];
    _pickCount = count;
    _pickSel = 0;
    _pickScroll = 0;
    _pickLoaded = true;
  }
  int consumePendingSend() {
    int idx = _pendingSendIdx;
    _pendingSendIdx = -1;
    return idx;
  }
  const char* pendingSendName() const { return _pendingSendName; }

  // ---- Send (called by main.cpp after consumePendingSend) ----
  // VE3 envelopes are still parsed on receive (from Meck-P4 and Meck).
  static uint32_t fromBase36(const char* s) {
    uint32_t val = 0;
    while (*s) {
      val *= 36;
      char c = *s++;
      if (c >= '0' && c <= '9') val += c - '0';
      else if (c >= 'a' && c <= 'z') val += 10 + c - 'a';
      else if (c >= 'A' && c <= 'Z') val += 10 + c - 'A';
    }
    return val;
  }

  // Caches the open note as the outgoing session (all send modes).
  bool prepareOut(uint32_t sessionId) {
    if (_c2Bytes == 0) return false;
    _out.sessionId = sessionId;
    memcpy(_out.data, _c2, _c2Bytes);
    _out.dataBytes = _c2Bytes;
    _out.totalPackets = (_c2Bytes + WV_MESH_PAYLOAD - 1) / WV_MESH_PAYLOAD;
    _out.cachedAt = millis();
    _out.active = true;
    Serial.printf("Voice: session 0x%08lX cached, %lu bytes, %d packets\n",
                  (unsigned long)sessionId, (unsigned long)_c2Bytes, _out.totalPackets);
    return true;
  }
  uint8_t outDuration() const {
    return (uint8_t)((_out.dataBytes / WV_FRAME_BYTES) * WV_FRAME_MS / 1000);
  }

  // ---- Flood-mode messages (see the WV_MSG_* table at the top) ----
  int buildFloodHeader(uint8_t* buf, int bufLen, const char* myName) const {
    if (!_out.active) return 0;
    int nameLen = myName ? (int)strnlen(myName, 31) : 0;
    if (8 + nameLen > bufLen) return 0;
    buf[0] = WV_PKT_MAGIC;
    buf[1] = WV_MSG_HEADER;
    memcpy(&buf[2], &_out.sessionId, 4);
    buf[6] = _out.totalPackets;
    buf[7] = outDuration();
    if (nameLen) memcpy(&buf[8], myName, nameLen);
    return 8 + nameLen;
  }

  int buildFloodData(uint8_t* buf, int bufLen, uint8_t pktIdx, bool wantReport) const {
    if (!_out.active) return 0;
    uint32_t offset = (uint32_t)pktIdx * WV_MESH_PAYLOAD;
    if (offset >= _out.dataBytes) return 0;
    uint32_t chunk = _out.dataBytes - offset;
    if (chunk > WV_MESH_PAYLOAD) chunk = WV_MESH_PAYLOAD;
    if ((int)(WV_DATA_HDR_SIZE + chunk) > bufLen) return 0;
    buf[0] = WV_PKT_MAGIC;
    buf[1] = WV_MSG_DATA;
    memcpy(&buf[2], &_out.sessionId, 4);
    buf[6] = pktIdx | (wantReport ? WV_FLAG_REPORT : 0);
    buf[7] = _out.totalPackets;
    buf[8] = (uint8_t)chunk;
    memcpy(&buf[WV_DATA_HDR_SIZE], &_out.data[offset], chunk);
    return WV_DATA_HDR_SIZE + chunk;
  }

  int buildStatusRequest(uint8_t* buf, int bufLen) const {
    if (!_out.active || bufLen < 6) return 0;
    buf[0] = WV_PKT_MAGIC;
    buf[1] = WV_MSG_STATUS;
    memcpy(&buf[2], &_out.sessionId, 4);
    return 6;
  }

  uint32_t outSessionId() const { return _out.sessionId; }

  // A contact's report on our current outgoing note. Returns false if it
  // isn't a report for that note.
  bool parseReport(const uint8_t* d, uint8_t len, uint16_t& bitmap, uint8_t& total) const {
    if (len < WV_REPORT_SIZE || d[0] != WV_PKT_MAGIC || d[1] != WV_MSG_REPORT) return false;
    uint32_t sid;
    memcpy(&sid, &d[2], 4);
    if (!_out.active || sid != _out.sessionId) return false;
    bitmap = (uint16_t)d[6] | ((uint16_t)d[7] << 8);
    total = d[8];
    return true;
  }

  int buildVoicePacket(uint8_t* buf, int bufLen, uint32_t sessionId, uint8_t pktIdx) const {
    if (!_out.active || _out.sessionId != sessionId) return 0;
    uint32_t offset = (uint32_t)pktIdx * WV_MESH_PAYLOAD;
    if (offset >= _out.dataBytes) return 0;
    uint32_t chunk = _out.dataBytes - offset;
    if (chunk > WV_MESH_PAYLOAD) chunk = WV_MESH_PAYLOAD;
    if ((int)(WV_PKT_HDR_SIZE + chunk) > bufLen) return 0;
    buf[0] = WV_PKT_MAGIC;
    memcpy(&buf[1], &sessionId, 4);
    buf[5] = pktIdx;
    memcpy(&buf[6], &_out.data[offset], chunk);
    return WV_PKT_HDR_SIZE + chunk;
  }
  uint8_t outPacketCount() const { return _out.active ? _out.totalPackets : 0; }
  bool hasOutSession(uint32_t sessionId) const {
    return _out.active && _out.sessionId == sessionId &&
           (millis() - _out.cachedAt) < WV_SESSION_TTL_MS;
  }

  // After a send: a recorded note becomes "To <name>".
  void onSent(const char* toName) {
    if (_curKind == KIND_RECORDED && _curName[0]) {
      uint8_t patch[1 + 32];
      memset(patch, 0, sizeof(patch));
      patch[0] = KIND_SENT;
      patchHeader(_curName, 4, patch, 1);
      strncpy((char*)&patch[1], toName, 31);
      patchHeader(_curName, 12, &patch[1], 32);
      _curKind = KIND_SENT;
      kindLabel(_curKind, toName, _curLabel, sizeof(_curLabel));
    }
    _mode = VIEW;
  }
  void onSendFailed() { _mode = VIEW; }

  // ---- VE3 receive (called by main.cpp callbacks) ----
  void onVE3Received(const char* senderName, const char* ve3Text) {
    const char* p = ve3Text + 4;   // skip "VE3:"
    char fields[4][16];
    int fieldIdx = 0, charIdx = 0;
    memset(fields, 0, sizeof(fields));
    while (*p && fieldIdx < 4) {
      if (*p == ':') { fields[fieldIdx][charIdx] = '\0'; fieldIdx++; charIdx = 0; }
      else if (charIdx < 15) fields[fieldIdx][charIdx++] = *p;
      p++;
    }
    if (fieldIdx < 3) {
      Serial.printf("Voice: VE3 parse failed, %d fields\n", fieldIdx + 1);
      return;
    }
    if (fieldIdx < 4) fields[fieldIdx][charIdx] = '\0';
    uint32_t sessionId = fromBase36(fields[0]);
    uint8_t totalPkts = (uint8_t)fromBase36(fields[2]);
    uint8_t durSec = (fieldIdx >= 3) ? (uint8_t)fromBase36(fields[3]) : 0;
    startIncoming(sessionId, totalPkts, durSec, senderName, false);
  }

  // Starts receiving a note, unless it's the one already in progress or the
  // one just saved. Returns true if a new note was started.
  bool startIncoming(uint32_t sessionId, uint8_t totalPkts, uint8_t durSec,
                     const char* senderName, bool viaChannel) {
    if (totalPkts == 0 || totalPkts > WV_MAX_PACKETS) {
      Serial.printf("Voice: invalid packet count %d\n", totalPkts);
      return false;
    }
    if (_lastSavedSid != 0 && sessionId == _lastSavedSid) return false;
    if (_in.active && _in.sessionId == sessionId) {
      // Already receiving it: a later header fills in the sender's name.
      if (senderName && senderName[0] && strcmp(_in.senderName, "?") == 0) {
        strncpy(_in.senderName, senderName, 31);
        _in.senderName[31] = 0;
      }
      if (durSec) _in.durationSec = durSec;
      return false;
    }
    memset(&_in, 0, sizeof(_in));
    _in.sessionId = sessionId;
    _in.totalPackets = totalPkts;
    _in.durationSec = durSec;
    strncpy(_in.senderName, (senderName && senderName[0]) ? senderName : "?", 31);
    _in.startedAt = millis();
    _in.active = true;
    _in.viaChannel = viaChannel;
    Serial.printf("Voice: incoming 0x%08lX from %s, %d packets (%ds)%s\n",
                  (unsigned long)sessionId, _in.senderName, totalPkts, durSec,
                  viaChannel ? " on channel" : "");
    return true;
  }

  // Raw VE3 voice packet: [0x56][sid:4][idx][Codec2...]
  void onVoicePacketReceived(const uint8_t* payload, uint8_t len) {
    if (len < WV_PKT_HDR_SIZE + 1) return;
    uint32_t sessionId;
    memcpy(&sessionId, &payload[1], 4);
    addPacket(sessionId, payload[5], &payload[WV_PKT_HDR_SIZE], len - WV_PKT_HDR_SIZE);
  }

  void addPacket(uint32_t sessionId, uint8_t idx, const uint8_t* data, uint8_t dataLen) {
    if (!_in.active || _in.sessionId != sessionId) {
      if (sessionId != _lastSavedSid)
        Serial.printf("Voice: packet for unknown session 0x%08lX\n", (unsigned long)sessionId);
      return;
    }
    if (idx >= _in.totalPackets || idx >= WV_MAX_PACKETS) return;
    if (_in.receivedBitmap & (1 << idx)) return;            // duplicate
    if (_in.dataBytes + dataLen > WV_MAX_BYTES) return;
    _in.pktOffset[idx] = _in.dataBytes;
    _in.pktSize[idx] = dataLen;
    memcpy(&_in.data[_in.dataBytes], data, dataLen);
    _in.dataBytes += dataLen;
    _in.receivedBitmap |= (1 << idx);
    _in.receivedCount++;
    Serial.printf("Voice: packet %d/%d (%d bytes)\n",
                  _in.receivedCount, _in.totalPackets, dataLen);
    if (_in.receivedCount >= _in.totalPackets) _in.complete = true;
  }

  // Flood data message ('D'). Returns true if it started a new note.
  bool onFloodData(const uint8_t* d, uint8_t len, const char* senderName, bool viaChannel) {
    if (len < WV_DATA_HDR_SIZE + 1) return false;
    uint32_t sid;
    memcpy(&sid, &d[2], 4);
    uint8_t idx = d[6] & ~WV_FLAG_REPORT;
    uint8_t total = d[7];
    uint8_t dlen = d[8];
    if (WV_DATA_HDR_SIZE + dlen > len) return false;      // malformed
    bool started = startIncoming(sid, total, 0, senderName, viaChannel);
    addPacket(sid, idx, &d[WV_DATA_HDR_SIZE], dlen);
    return started;
  }

  // Voice channel message (header or data). Returns true if a new note started.
  bool onChannelVoice(const uint8_t* d, uint8_t len) {
    if (len < 6 || d[0] != WV_PKT_MAGIC) return false;
    if (d[1] == WV_MSG_HEADER && len >= 8) {
      uint32_t sid;
      memcpy(&sid, &d[2], 4);
      char name[32];
      int n = len - 8;
      if (n > 31) n = 31;
      memcpy(name, &d[8], n);
      name[n] = 0;
      return startIncoming(sid, d[6], d[7], name, true);
    }
    if (d[1] == WV_MSG_DATA) return onFloodData(d, len, "?", true);
    return false;
  }

  // Report on a note from this contact, written to 'reply'.
  uint8_t buildReport(uint32_t sid, uint8_t* reply) const {
    uint16_t bitmap = 0;
    uint8_t total = 0;
    if (_in.sessionId == sid && (_in.active || _in.saved)) {
      bitmap = _in.receivedBitmap; total = _in.totalPackets;
    } else if (sid == _lastSavedSid) {
      bitmap = _lastSavedBitmap; total = _lastSavedTotal;
    }
    reply[0] = WV_PKT_MAGIC;
    reply[1] = WV_MSG_REPORT;
    memcpy(&reply[2], &sid, 4);
    reply[6] = (uint8_t)(bitmap & 0xFF);
    reply[7] = (uint8_t)(bitmap >> 8);
    reply[8] = total;
    return WV_REPORT_SIZE;
  }

  // Voice request from a contact (data or status). Returns the reply length
  // (0 = no reply); 'started' is set if a new note started.
  uint8_t onContactVoice(const char* senderName, const uint8_t* d, uint8_t len,
                         uint8_t* reply, bool& started) {
    started = false;
    if (len < 6 || d[0] != WV_PKT_MAGIC) return 0;
    uint32_t sid;
    memcpy(&sid, &d[2], 4);
    if (d[1] == WV_MSG_DATA) {
      started = onFloodData(d, len, senderName, false);
      if (len > 6 && (d[6] & WV_FLAG_REPORT)) return buildReport(sid, reply);
      return 0;
    }
    if (d[1] == WV_MSG_STATUS) return buildReport(sid, reply);
    return 0;
  }

  bool isIncomingReady() const { return _in.active && _in.complete && !_in.saved; }
  const char* incomingSender() const { return _in.senderName; }
  bool incomingViaChannel() const { return _in.viaChannel; }

  // Saves a completed incoming note (packets reassembled in order) as a new,
  // unread list entry. Returns true on success.
  bool saveIncoming() {
    _in.saved = true;
    uint8_t ordered[WV_MAX_BYTES];
    uint32_t len = 0;
    for (int p = 0; p < _in.totalPackets && p < WV_MAX_PACKETS; p++) {
      if (_in.pktSize[p] > 0 && len + _in.pktSize[p] <= WV_MAX_BYTES) {
        memcpy(&ordered[len], &_in.data[_in.pktOffset[p]], _in.pktSize[p]);
        len += _in.pktSize[p];
      }
    }
    uint8_t dur = (uint8_t)((len / WV_FRAME_BYTES) * WV_FRAME_MS / 1000);
    char leaf[WV_NAME_LEN];
    makeName(leaf, sizeof(leaf));
    bool ok = writeNote(leaf, _in.viaChannel ? KIND_CH_RECEIVED : KIND_RECEIVED, 1, dur,
                        _in.senderName, ordered, len);
    Serial.printf("Voice: saved incoming %s, %lu bytes from %s: %s\n",
                  leaf, (unsigned long)len, _in.senderName, ok ? "OK" : "FAILED");
    _lastSavedSid = _in.sessionId;
    _lastSavedBitmap = _in.receivedBitmap;
    _lastSavedTotal = _in.totalPackets;
    _in.active = false;
    if (ok && _mode == LIST) scanNotes();
    return ok;
  }

  // ---- Input ----
  // Physical tap in logical (display.width()) coordinates.
  bool handleTap(int lx, int ly) {
    switch (_mode) {
      case LIST:
        if (ly >= ROW_Y0 && ly < FOOTER_Y) {
          int row = (ly - ROW_Y0) / ROW_H;
          int idx = _scroll + row;
          if (row >= 0 && row < ROWS_VISIBLE && idx < _count) {
            if (idx == _sel) openSelected();             // second tap opens
            else { _sel = idx; ensureVisible(); }        // first tap selects
          }
        }
        return true;

      case VIEW:
        if (ly >= FOOTER_Y) {
          if (lx < 40) {                                  // Back
            stopPlayback();
            _delArmed = false;
            _mode = LIST;
            scanNotes();
          } else if (lx < 80) {                           // Send
            stopPlayback();
            _delArmed = false;
            _pickLoaded = false;                          // main.cpp fills it
            _mode = PICK;
          } else {                                        // Del (asks once)
            if (_delArmed) deleteCurrent();
            else _delArmed = true;
          }
          return true;
        }
        _delArmed = false;
        if (_decoding || _playing) stopPlayback();        // tap = play / stop
        else startPlayback();
        return true;

      case PICK:
        if (ly >= FOOTER_Y) {
          if (lx >= 31 && lx < 89) _mode = VIEW;          // Back
          return true;
        }
        if (_pickLoaded && ly >= ROW_Y0) {
          int row = (ly - ROW_Y0) / ROW_H;
          int idx = _pickScroll + row;
          if (row >= 0 && row < ROWS_VISIBLE && idx < _pickCount) {
            if (idx != _pickSel) { _pickSel = idx; }      // first tap selects
            else {                                        // second tap sends
              _pendingSendIdx = _pick[idx].meshIdx;
              strncpy(_pendingSendName, _pick[idx].name, 31);
              _pendingSendName[31] = 0;
            }
          }
        }
        return true;

      default:
        return true;   // RECORDING / ENCODING: taps ignored (PWR stops)
    }
  }

  // The PWR key short press arrives as KEY_ENTER.
  bool handleInput(char c) override {
    if (c != KEY_ENTER) {
      return _mode != LIST;            // only the list lets other keys fall through
    }
    if (_mode == LIST) { startRecording(); return true; }
    if (_mode == RECORDING) { stopRecording(true); return true; }
    return true;                       // swallowed everywhere else
  }

  void poll() override {
    switch (_mode) {
      case RECORDING: {
        if (_pcmSamples < WV_MAX_SAMPLES) {
          _pcmSamples += _audio.micRead(&_pcm[_pcmSamples], WV_MAX_SAMPLES - _pcmSamples);
        }
        if (_pcmSamples >= WV_MAX_SAMPLES) stopRecording(false);   // 12 s cap
        break;
      }
      case ENCODING:
        encodeStep();
        break;
      case VIEW:
        if (_decoding) decodeStep();
        else if (_playing) playStep();
        break;
      default:
        break;
    }
  }

  int render(DisplayDriver& display) override {
    switch (_mode) {
      case LIST:      renderList(display);      return 500;
      case RECORDING: renderRecording(display); return 250;
      case ENCODING:  renderEncoding(display);  return 100;
      case VIEW:      renderView(display);      return (_decoding || _playing) ? 250 : 500;
      case PICK:      renderPick(display);      return 500;
    }
    return 500;
  }
};

#endif  // MECK_WATCH_VOICE
