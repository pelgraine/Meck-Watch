#pragma once
#include <cstdint> // For uint8_t, uint32_t
#include <helpers/ConfigSerializer.h>

#define TELEM_MODE_DENY            0
#define TELEM_MODE_ALLOW_FLAGS     1     // use contact.flags
#define TELEM_MODE_ALLOW_ALL       2

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1

// Per-channel notification preferences (stored in channel_notif[])
#define NOTIF_ALL       0   // Notify on all messages (default)
#define NOTIF_MENTIONS  1   // Notify only when @nodename appears in message
#define NOTIF_NONE      2   // No notifications (muted)

// Fixed channel_notif[] index for the DM slot. The array is sized [21]
// (20 group channels + 1 DM slot) regardless of MAX_GROUP_CHANNELS, so the
// DM slot must NOT be computed as MAX_GROUP_CHANNELS -- on 40-channel builds
// that lands out of bounds and the DM preference silently never applies.
#define DM_NOTIF_SLOT   20

// Canned messages (T-Watch): slot count and per-slot buffer size.
// CANNED_MSG_LEN matches TW_OUT_BUF_LEN -- the 133-char channel message cap
// plus a NUL terminator.
#define CANNED_MSG_SLOTS  5
#define CANNED_MSG_LEN    134

// Persisted to /prefs.json via ConfigSerializer (upstream MeshCore v1.17.0, PR #2982).
// Older binary /new_prefs files are read once by DataStore::loadPrefsInt() and
// re-saved as JSON on the next boot. All scalar fields carry an in-class default
// equal to the previous memset-0 state -- the class now has a vtable, so it must
// NOT be memset/memcpy'd wholesale. MyMesh's constructor still applies the
// non-zero radio/name defaults on top, exactly as before.
class NodePrefs : public ConfigSerializer {  // persisted to file
public:
  float airtime_factor = 0;
  char node_name[32];
  // Node position. Previously carried alongside the prefs blob by DataStore
  // (offsets 40/48); now a plain pair of fields, mirrored to/from
  // sensors.node_lat / node_lon by MyMesh::begin() and MyMesh::savePrefs().
  double node_lat = 0, node_lon = 0;
  float freq = 0;
  uint8_t sf = 0;
  uint8_t cr = 0;
  uint8_t multi_acks = 0;
  uint8_t manual_add_contacts = 0;
  float bw = 0;
  uint8_t tx_power_dbm = 0;
  uint8_t telemetry_mode_base = 0;
  uint8_t telemetry_mode_loc = 0;
  uint8_t telemetry_mode_env = 0;
  float rx_delay_base = 0;
  uint32_t ble_pin = 0;
  uint8_t  advert_loc_policy = 0;
  uint8_t  buzzer_quiet = 0;
  uint8_t  gps_enabled = 0;      // GPS enabled flag (0=disabled, 1=enabled)
  uint32_t gps_interval = 0;     // GPS read interval in seconds
  uint8_t autoadd_config = 0;    // bitmask for auto-add contacts config
  int8_t  utc_offset_hours = 0;  // UTC offset in hours (-12 to +14), default 0
  uint8_t kb_flash_notify = 0;   // Keyboard backlight flash on new message (0=off, 1=on)
  uint8_t ringtone_enabled = 0;  // Ringtone on incoming call (0=off, 1=on) — 4G only
  uint8_t path_hash_mode = 0;    // 0=1-byte (legacy), 1=2-byte, 2=3-byte path hashes
  uint8_t autoadd_max_hops = 0;  // 0=no limit, N=up to N-1 hops (max 64)
  uint32_t gps_baudrate = 0;     // GPS baud rate (0 = use compile-time GPS_BAUDRATE default)
  uint8_t interference_threshold = 0; // Interference threshold in dB (0=disabled, 14+=enabled)
  // Radio settings from upstream's app command line (CMD_RUN_CLI_COMMAND),
  // saved under upstream's JSON keys in "radio". The defaults keep the
  // watch's behaviour from before they existed: power-saving RX gain, CAD
  // off, AGC reset off; the TX delay factors get 0.5 / 0.2 in the MyMesh
  // constructor (the values the delay code used before).
  uint8_t rx_boosted_gain = 0;        // 0 = power-saving RX gain, 1 = boosted
  uint8_t cad_enabled = 0;            // hardware CAD before transmit
  uint8_t agc_reset_interval = 0;     // seconds / 4, 0 = off
  float tx_delay_factor = 0;          // flood retransmit delay factor (txdelay)
  float direct_tx_delay_factor = 0;   // direct retransmit delay factor (direct.txdelay)
  uint8_t dark_mode = 0;              // 0=off (white bg), 1=on (black bg)
  uint8_t portrait_mode = 0;          // 0=landscape, 1=portrait — T5S3 only
  uint8_t auto_lock_minutes = 0;      // 0=disabled, 2/5/10/15/30=auto-lock after idle
  uint8_t hint_shown = 0;             // 0=show nav hint on boot, 1=already shown (dismiss permanently)
  uint8_t large_font = 0;             // 0=tiny (built-in 6x8), 1=larger (FreeSans9pt) — T-Deck Pro only
  uint8_t ui_font_style = 0;          // 0=Classic (FreeSans), 1=Noto Sans, 2=Montserrat
  uint8_t tx_fail_reset_threshold = 0;  // 0=disabled, 1-10, default 3
  uint8_t rx_fail_reboot_threshold = 0; // 0=disabled, 1-10, default 3

  // --- Region scope (MeshCore v1.15+ compatibility) ---
  // Device-wide default region for flood messages.
  // Empty default_scope_name = unscoped (legacy flood, reaches all repeaters).
  // When set, all flood sends use ROUTE_TYPE_TRANSPORT_FLOOD with transport
  // codes derived from this key.  Per-channel scope (in ChannelDetails) takes
  // priority when set.
  char default_scope_name[31];     // e.g. "au-nsw", empty = unscoped
  uint8_t default_scope_key[16];   // TransportKey derived from "#" + name

  // --- Per-channel notification preferences ---
  // Index 0..19 for group channels, index DM_NOTIF_SLOT (20) for DMs.
  // Values: NOTIF_ALL (0), NOTIF_MENTIONS (1), NOTIF_NONE (2).
  // Defaults to NOTIF_ALL for all channels.
  uint8_t channel_notif[21];       // 20 group channels + 1 DM slot

  // --- LoRa antenna selection (T-Deck Pro MAX only) ---
  // 0 = internal antenna (XL9555 LORA_SEL HIGH), 1 = external (LORA_SEL LOW).
  // Applied at boot in main.cpp and live on toggle in SettingsScreen.
  uint8_t lora_antenna = 0;            // 0 = internal (default), 1 = external

  // --- Backlight (e-ink frontlight) brightness, T-Deck Pro MAX only ---
  // Percentage (5..100) the heart button toggles the frontlight on to.
  // 0 = unset/legacy; treated as the default 100 on load.
  uint8_t backlight_brightness_pct = 0; // 5..100, default 100

  // --- Keyboard LED brightness, T-Deck Pro MAX only ---
  // Percentage (5..100) the both-shifts toggle turns the keyboard backlight on to.
  // Takes effect on next toggle. Default 50.
  uint8_t kb_backlight_pct = 0;        // 5..100, default 50

  // --- MCU-driven RX duty cycle (T-Watch S3 / S3 Plus) ---
  // Runtime toggle for the experimental RX duty-cycle power saving.
  // 0 = off (default), 1 = on. Off by default; older prefs files short-read
  // this final field and it keeps its memset-0 default (see DataStore.cpp).
  uint8_t rx_duty_cycle = 0;           // 0 = off (default), 1 = on

  // Nap depth for the RX duty cycle: 0 = Light (~31% asleep), 1 = Medium
  // (~50%), 2 = Aggressive (~64%). Persists independently of rx_duty_cycle, so
  // the last-chosen depth is restored when the feature is switched back on.
  // Older prefs files short-read this final field and keep the memset-0 (Light)
  // default.
  uint8_t rx_duty_level = 0;           // 0 = Light (default), 1 = Medium, 2 = Aggressive

  // --- World clock slots (T-Watch) ---
  // Two extra UTC offsets shown on the world clock home page beside Home
  // (which uses utc_offset_hours). Whole hours, -12..+14. Older prefs files
  // short-read these final fields and keep the memset-0 default (UTC+0).
  int8_t clock_slot_a = 0;             // Zone 1 UTC offset, default 0
  int8_t clock_slot_b = 0;             // Zone 2 UTC offset, default 0

  // --- Position sharing for adverts (T-Watch) ---
  // Controls whether self-adverts carry a position, and where it comes from:
  //   0 = Off     -- no position shared
  //   1 = Manual  -- user-entered lat/lon; GPS must not overwrite them
  //   2 = Auto    -- GPS keeps lat/lon updated (GPS must be on)
  // The coordinates themselves are sensors.node_lat / node_lon, already
  // persisted at prefs offsets 40/48. Older prefs files short-read this final
  // field and keep the memset-0 (Off) default.
  uint8_t position_mode = 0;           // 0 = Off (default), 1 = Manual, 2 = Auto-GPS

  // --- Canned messages (T-Watch) ---
  // Five pre-written channel messages, edited in Settings -> Canned Messages
  // and sent from the channel compose keyboard via the physical button. Each
  // slot holds up to 133 chars + NUL (the per-channel message cap); an empty
  // string marks an unused slot. Older prefs files short-read this final
  // field and keep the memset-0 (all slots empty) default.
  char canned_msgs[CANNED_MSG_SLOTS][CANNED_MSG_LEN];

private:
  // --- ConfigSerializer layout ---
  // Key names for the fields shared with upstream follow upstream's companion
  // NodePrefs.h (radio / gps / comp objects) so a prefs.json written by either
  // firmware is readable by the other; unknown keys are ignored on load and
  // missing keys keep their defaults. Meck-only fields live under "meck",
  // canned messages under "canned". Keys must be <= 15 chars, letters and
  // underscore only (ConfigSerializer parser rules).
  class RadioPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("freq", _parent->freq);
      def("bw", _parent->bw);
      def("sf", _parent->sf);
      def("cr", _parent->cr);
      def("cad", _parent->cad_enabled);
      def("rxgain", _parent->rx_boosted_gain);
      def("tx", _parent->tx_power_dbm);
      def("af", _parent->airtime_factor);
      def("rxdelay", _parent->rx_delay_base);
      def("f_txdelay", _parent->tx_delay_factor);
      def("d_txdelay", _parent->direct_tx_delay_factor);
      def("agc_int", _parent->agc_reset_interval);
      def("hash_mode", _parent->path_hash_mode);
      def("multi_ack", _parent->multi_acks);
    }
  public:
    RadioPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  RadioPrefs radio;

  class GPSPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("en", _parent->gps_enabled);       // boolean
      def("int", _parent->gps_interval);     // interval in seconds
      def("adv_loc", _parent->advert_loc_policy);
    }
  public:
    GPSPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  GPSPrefs gps;

  class RepeatPrefs : public ConfigSerializer {  // COPIED from CommonCLI (for now)
  public:
    uint8_t disable_fwd = 1;
  protected:
    void structure() override {
      def("disable", disable_fwd);
      //def("f_max", flood_max);
      //def("f_max_uns", flood_max_unscoped);
      //def("f_max_adv", flood_max_advert);
      //def("loop", loop_detect);
    }
  };
  RepeatPrefs repeat;

  class CompanionPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("auto_max", _parent->autoadd_max_hops);
      def("defs_nm", _parent->default_scope_name, sizeof(_parent->default_scope_name));
      def("defs_key", (void *) _parent->default_scope_key, sizeof(_parent->default_scope_key));
      def("pin", _parent->ble_pin);
      def("buzz_q", _parent->buzzer_quiet);
      def("auto_add", _parent->autoadd_config);
      def("man_add", _parent->manual_add_contacts);
      def("tel_base", _parent->telemetry_mode_base);
      def("tel_loc", _parent->telemetry_mode_loc);
      def("tel_env", _parent->telemetry_mode_env);
    }
  public:
    CompanionPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  CompanionPrefs companion;

  class MeckPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("utc_off", _parent->utc_offset_hours);
      def("kb_flash", _parent->kb_flash_notify);
      def("ringtone", _parent->ringtone_enabled);
      def("gps_baud", _parent->gps_baudrate);
      def("int_thresh", _parent->interference_threshold);
      def("dark", _parent->dark_mode);
      def("portrait", _parent->portrait_mode);
      def("auto_lock", _parent->auto_lock_minutes);
      def("hint_shown", _parent->hint_shown);
      def("large_font", _parent->large_font);
      def("font_style", _parent->ui_font_style);
      def("txfail_rst", _parent->tx_fail_reset_threshold);
      def("rxfail_rbt", _parent->rx_fail_reboot_threshold);
      def("chan_notif", (void *) _parent->channel_notif, sizeof(_parent->channel_notif));
      def("lora_ant", _parent->lora_antenna);
      def("backlight", _parent->backlight_brightness_pct);
      def("kb_backlight", _parent->kb_backlight_pct);
      def("rx_duty", _parent->rx_duty_cycle);
      def("rx_duty_lvl", _parent->rx_duty_level);
      def("clock_a", _parent->clock_slot_a);
      def("clock_b", _parent->clock_slot_b);
      def("pos_mode", _parent->position_mode);
    }
  public:
    MeckPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  MeckPrefs meck;

  // Five canned-message slots as five string values ("a".."e"). Each value
  // may be up to CANNED_MSG_LEN-1 chars, which exceeds ConfigSerializer's
  // default 128-char token buffer -- CONFIG_MAX_TOKEN_LEN is raised to 160
  // via a build flag (platformio.ini) so the parser can read them back.
  class CannedPrefs : public ConfigSerializer {
    NodePrefs* _parent;
  protected:
    void structure() override {
      def("a", _parent->canned_msgs[0], CANNED_MSG_LEN);
      def("b", _parent->canned_msgs[1], CANNED_MSG_LEN);
      def("c", _parent->canned_msgs[2], CANNED_MSG_LEN);
      def("d", _parent->canned_msgs[3], CANNED_MSG_LEN);
      def("e", _parent->canned_msgs[4], CANNED_MSG_LEN);
    }
  public:
    CannedPrefs(NodePrefs* parent) : _parent(parent) { }
  };
  CannedPrefs canned;

protected:
  void structure() override {
    def("name", node_name, sizeof(node_name));
    def("lat", node_lat);
    def("lon", node_lon);
    def("radio", radio);
    def("gps", gps);
    def("repeat", repeat);
    def("comp", companion);
    def("meck", meck);
    def("canned", canned);
  }

public:
  NodePrefs() : radio(this), gps(this), companion(this), meck(this), canned(this) {
    node_name[0] = 0;
    default_scope_name[0] = 0;
    memset(default_scope_key, 0, sizeof(default_scope_key));
    memset(channel_notif, 0, sizeof(channel_notif));
    memset(canned_msgs, 0, sizeof(canned_msgs));
  }

  // Client repeat mode (companion flood forwarding), upstream MeshCore.
  // Stored inverted as repeat.disable_fwd, same as upstream, so a prefs.json
  // is readable by either firmware.
  bool isRepeatEn() const { return repeat.disable_fwd == 0; }
  void setRepeatEn(bool en) { repeat.disable_fwd = en ? 0 : 1; }

  // --- Font helpers (inline, no overhead) ---
  // Returns the DisplayDriver text-size index for "small/body" text.
  // T-Deck Pro: 0 = built-in 6×8 (or 7pt with custom fonts), 1 = 9pt.
  // T5S3: both 0 and 1 are 12pt fonts (regular vs bold) with identical line
  //        height, so large_font has no layout effect there.
  inline uint8_t smallTextSize() const {
    return large_font ? 1 : 0;
  }

  // Returns the virtual-coordinate line height matching smallTextSize().
  // T-Deck Pro size 0 → 9 (6×8 + 1px gap), size 1 → 11 (9pt ascent+descent).
  // With custom fonts (Noto 7pt, Montserrat 7pt), size 0 is slightly taller
  // than built-in 6×8 but fits within the 9-unit virtual grid.
  // T5S3 size 0/1 → same 12pt height → always 9 in virtual coords.
  inline int smallLineH() const {
    return large_font ? 11 : 9;
  }

  // Returns the Y offset for selection highlight fillRect (T-Deck Pro only).
  // Size 0 (built-in font): cursor positions at top-left, +5 offset in
  //   setCursor places text below → fillRect at y+5 aligns with text.
  // Size 0 (custom 7pt fonts): baseline fonts, same behaviour as size 1.
  // Size 1 (9pt): cursor positions at baseline, ascenders render
  //   upward → fillRect must start above baseline to cover ascenders.
  // T5S3: always 0 (both sizes use baseline fonts with highlight at y).
  inline int smallHighlightOff() const {
#if defined(MECK_TWATCH)
    return 0;
#else
    // Custom 7pt fonts at textSize 0 use GFXfont (baseline rendering), not built-in.
    // Offset 0: highlight starts at row top, covers 7pt ascenders without
    // bleeding into the row above (-2 was calibrated for taller 9pt ascenders).
    if (ui_font_style > 0 && !large_font) return 0;
    return large_font ? -2 : 5;
#endif
  }
};