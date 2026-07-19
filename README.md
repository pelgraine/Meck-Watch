## Meshcore + Fork = Meck (T-Watch Edition)

A MeshCore companion-radio firmware for the **LilyGo T-Watch S3** and **T-Watch S3 Plus** — wrist-worn, touch-first mesh nodes. This is the watch-only build of Meck; the multi-device Meck firmware (T-Deck Pro, T-Deck Max, T5S3 E-Paper Pro, Heltec repeaters) lives in the [main Meck repository](https://github.com/pelgraine/Meck). Created with Claude AI on MeshCore v1.11 code.

[Check out the Meck discussion channel on the MeshCore Discord](https://discord.com/channels/1495203904898728149/1496789639556501614)

### Contents
- [Supported Devices](#supported-devices)
- [S3 vs S3 Plus at a Glance](#s3-vs-s3-plus-at-a-glance)
- [No SD Card](#no-sd-card)
- [Build Variants](#build-variants)
- [Flashing Firmware](#flashing-firmware)
- [Home Screen & Navigation](#home-screen--navigation)
- [The Buttons — S3 vs S3 Plus](#the-buttons--s3-vs-s3-plus)
- [Touch Gestures](#touch-gestures)
- [Tilt-to-Wake & Lock Screen](#tilt-to-wake--lock-screen)
- [Step Counter](#step-counter)
- [Clock & Timezone](#clock--timezone)
- [Channel Messages & the Channel Picker](#channel-messages--the-channel-picker)
- [Contacts & Direct Messages](#contacts--direct-messages)
- ["Heard By" on Sent Messages](#heard-by-on-sent-messages)
- [Notes](#notes)
- [Discovery & Last Heard](#discovery--last-heard)
- [Rx Log](#rx-log)
- [Repeater Admin & Path Editor](#repeater-admin--path-editor)
- [Trace Route](#trace-route)
- [Games](#games)
- [Maps (S3 Plus only)](#maps-s3-plus-only)
- [Settings](#settings)
- [Path Hash Mode](#path-hash-mode)
- [Region Scope](#region-scope)
- [Channels: Public, Private & Sharing](#channels-public-private--sharing)
- [Bluetooth (BLE)](#bluetooth-ble)
- [Battery & Charging](#battery--charging)
- [About MeshCore](#about-meshcore)
- [MeshCore Clients](#meshcore-clients)
- [Contributing](#contributing)
- [Get Support](#-get-support)
- [License](#-license)
  - [Third-Party Libraries](#third-party-libraries)

---

## Supported Devices

Meck (T-Watch Edition) targets two LilyGo watches, both built on the ESP32-S3 (16 MB flash, 8 MB PSRAM) with an ST7789 1.54" 240×240 IPS panel, FT6336U capacitive touch, AXP2101 PMU, BMA423 accelerometer, DRV2605 haptic motor, PCF8563 hardware RTC, and an SX1262 LoRa radio.

| Device | Display | Input | LoRa | Battery | GPS | RTC |
|--------|---------|-------|------|---------|-----|-----|
| **T-Watch S3** | 240×240 IPS (ST7789, LovyanGFX) | FT6336U touch + PWR key (single button) | SX1262 | AXP2101 + BMA423, 470 mAh LiHV | No | Yes (PCF8563) |
| **T-Watch S3 Plus** | 240×240 IPS (ST7789, LovyanGFX) | FT6336U touch + PWR key + BOOT + RESET buttons | SX1262 | AXP2101 + BMA423, LiHV | Yes (MIA-M10Q) | Yes (PCF8563) |

The two boards are wired identically at the ESP32-S3 — they differ only in what is *populated*, not in how the chip is connected.

---

## S3 vs S3 Plus at a Glance

| | T-Watch S3 | T-Watch S3 Plus |
|---|---|---|
| GPS | None (no map screen) | Onboard MIA-M10Q GNSS — map screen available |
| Physical buttons | **One: the side PWR key only** | **Three: PWR key, plus BOOT (GPIO0) and RESET** on the back shell |
| Button Meck reads | PWR key (via the PMU) | The GPIO0 boot button |
| Home tile (row 3, right) | **Games** (Trace + Games) | **Maps** (Trace + Maps) |
| Battery cell | 470 mAh LiHV (Tewaycell 552530, 3.8 V nom / 4.35 V max) | LiHV (Tewaycell 112530-2P, 3.8 V nom / 4.35 V max) |
| Estimated runtime | ~7.5–9.5 hours | ~14–16 hours |
| Firmware version string | `Meck TWatch S3 v0.3` / `Meck TWatch S3 BLE v0.3` | `Meck TWatch v0.3` / `Meck TWatch BLE v0.3` |

Everything else — the UI, the touch keyboard, step counting, tilt-to-wake, the lock/clock screen, notes, and the whole channel/DM/contacts system — is shared between the two.

---

## No SD Card

Unlike the other Meck devices, the watches have **no SD card slot**. All watch-local data — notes, step history, and configuration — is stored on an internal LittleFS partition instead. There is nothing to insert, format, or manage.

Two consequences worth knowing:

- Features that are inherently SD-dependent on other Meck devices — the e-book/EPUB reader, audiobooks, and the web reader — are **not part of the watch build** at all.
- The **config export/import** menu items compile in but write to an SD card, so they are non-functional on the watch. Move config between devices using the companion app or the main-repo devices instead.

---

## Build Variants

| Variant | Environment | BLE | GPS | Max Contacts |
|---------|------------|-----|-----|-------------|
| S3 Standalone | `meck_twatch_s3_standalone` | — | No | 2,000 |
| S3 BLE | `meck_twatch_s3_ble` | Yes | No | 2,000 |
| S3 Plus Standalone | `meck_twatch_s3_plus_standalone` | — | Yes | 2,000 |
| S3 Plus BLE | `meck_twatch_s3_plus_ble` | Yes | Yes | 2,000 |

**Standalone** builds run the full touch UI with no phone connection — you can send and receive messages, browse contacts, and manage the mesh entirely from the watch. **BLE** builds add the MeshCore companion serial interface so a phone app can connect when you want it. There is no WiFi variant for the watches.

All four builds allocate their 2,000-contact arrays in PSRAM, and boot the CPU at 80 MHz (`ESP32_CPU_FREQ=80`) to conserve battery.

---

## Flashing Firmware

Download the latest firmware from the [Releases](https://github.com/pelgraine/Meck-Watch/releases) page. Each release includes two `.bin` types per variant:

| File Type | When to Use |
|-----------|-------------|
| `*-merged.bin` | **First-time flash** — bootloader, partition table, and firmware in one file. Flash at address `0x0`. |
| `*.bin` (non-merged) | **Upgrading existing firmware** — application image only. Flash at `0x10000`. |

The watches are ESP32-S3 devices and use the standard `esptool.py` workflow.

**First-time flash (merged):**

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0 meck_twatch_s3_standalone-merged.bin
```

**Upgrading (non-merged):**

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x10000 meck_twatch_s3_standalone.bin
```

On macOS the port is typically `/dev/cu.usbmodem*`; on Windows it will be a COM port like `COM3`.

> **Tip:** If you're unsure whether the device already has a bootloader, use the merged file at `0x0` — it overwrites everything cleanly.

> **First boot:** On a fresh flash the device formats its internal LittleFS partition. This takes a minute or two and only happens once.

> **No OTA on the watches.** Because there is no SD card to stage the image, firmware updates are done over USB. There is no over-the-air update path.

---

## Home Screen & Navigation

The home screen is a 4×2 grid of tappable tiles. Because the display is rendered in a 120×120 virtual coordinate space (the 240 px panel divided by `UI_ZOOM=2`), the touch targets are large and finger-friendly.

Six tiles are common to both watches — **Messages**, **Contacts**, **Settings**, **Discover**, **Trace**, **Notes**, and **Steps** — with the seventh slot differing by board:

- **S3 Plus** shows **Maps** (it has GPS).
- **S3** shows **Games** (Snake and Minesweeper) in that slot, since it has no GPS to plot.

Tap a tile to open its screen. Tap the **status bar** at the top of any screen to return home.

---

## The Buttons — S3 vs S3 Plus

This is the main behavioural difference between the two watches. The **S3 has a single button** (the side PWR key), so Meck reads that one; the **S3 Plus has three physical buttons** (PWR, plus BOOT and RESET on the back shell), and Meck reads its **BOOT button** for navigation.

**T-Watch S3** — the *only* control is the side **PWR key**, which is wired to the AXP2101 PMU rather than to a GPIO, so its events arrive over I2C:

| Press | Action |
|-------|--------|
| Short press | Enter / select / confirm. On the Contacts screen, opens the path editor for the selected contact. |
| Long press | Lock / unlock (a long press within the first 8 seconds of boot instead drops to the CLI rescue console). |
| Double press | Back / previous. |
| Hold ≥ 6 s | Hardware power-off (handled by the PMU; never reaches the firmware). |

**T-Watch S3 Plus** — Meck reads the **BOOT button (GPIO0)** on the back shell:

| Press | Action |
|-------|--------|
| Short press | Wake display / next (page). On the Contacts screen, opens the path editor for the selected contact. |
| Long press | Lock / unlock (CLI rescue in the first 8 seconds, as above). |
| Double press | Back / previous. |

The Plus's **RESET button** simply reboots the watch — it is a hardware reset that firmware cannot read or intercept. The Plus's **PWR key** remains the power control (press ~1 s from off to turn on; hold ~6 s to power off).

The net effect on navigation: on the **S3**, a short press acts as *Enter/select* (there is no separate page key, so paging is done by touch); on the **Plus**, a short press of the boot button acts as *page/next* and select is done by tapping.

---

## Touch Gestures

Touch is the primary way to drive both watches, and the gesture set follows the same conventions across screens:

| Gesture | Typical Action |
|---------|----------------|
| Tap | Select / activate / open the item under your finger |
| Swipe up / down | Scroll lists (contacts, messages, settings, discovery) |
| Swipe left / right | Adjust a value or cycle a filter, depending on screen |
| Long press | Context action (e.g. open a channel from the picker, DM a contact, open the keyboard for text entry) |
| Tap status bar | Return to home screen |

Text entry — composing messages, notes, admin passwords, typed paths — is done through the on-screen **touch keyboard**, which opens when you tap into an editable field.

---

## Tilt-to-Wake & Lock Screen

With the display off, a **wrist-raise** (detected by the BMA423 accelerometer) wakes the watch straight to the clock/lock screen and restarts the idle timer — no button press needed.

The lock screen shows the time, battery, and today's step count, and refreshes every 2 minutes while locked to keep the clock current. Unlock with a **long press** of the button (see [The Buttons](#the-buttons--s3-vs-s3-plus)).

---

## Step Counter

Both watches count steps continuously via the BMA423 and roll the daily total over at local midnight. The running total appears on the lock screen and behind the **Steps** home tile, and the step history is persisted to the internal LittleFS partition so it survives reboots.

---

## Clock & Timezone

The watches include a PCF8563 hardware RTC, so the clock keeps time across reboots once set. Set your **UTC offset** from the Settings screen. The valid range is UTC-12 to UTC+14 — for example, AEST is UTC+10 and AEDT is UTC+11. The offset is persisted and survives reboots, so you only set it once.

On the **S3 Plus**, the onboard GPS also provides a time source once it has a satellite fix; on the **S3** (no GPS), set the time from Settings or let a connected companion app push it.

If the RTC has fallen out of sync — most likely on the standalone **S3**, which has neither GPS nor an app connection to correct it — you can push the current time over USB serial with a clock-sync command carrying the current epoch time. See the [Serial Settings Guide](Serial%20Settings%20Guide.md) for the exact command and the full list of serial settings (region, path hash, radio parameters, and more).

---

## Channel Messages & the Channel Picker

A **long press on the home screen** opens the watch channel picker. It lists all your channels and the DM inbox in a single view with unread badges, so you can jump straight to a channel with new messages:

| Gesture | Action |
|---------|--------|
| Tap a row | Select that channel |
| Long press a row | Open the channel |
| Tap the back arrow (top-left) | Return home |

Inside a channel, scroll messages by swiping, tap to compose a new message via the touch keyboard, and long-press to go back. Replies and @mentions follow the standard MeshCore conventions.

---

## Contacts & Direct Messages

The Contacts screen lists all known nodes sorted by most recently heard, with a type prefix, estimated hop count, and time since last advert.

| Gesture | Action |
|---------|--------|
| Swipe up / down | Scroll contacts |
| Swipe left / right | Cycle the contact filter |
| Tap | Enter select mode (the tapped contact is pre-selected) |
| Long press on a chat contact | View unread DMs, then compose a DM |
| Long press on a repeater/room-server contact | Open repeater admin login |

**Select mode** lets you batch-operate on contacts — mark favourites or delete — from the overlay that appears after you exit select mode with contacts selected.

Sending a DM opens the touch keyboard; the message is tracked with the standard device-side retry counter and delivery status.

---

## "Heard By" on Sent Messages

When you send a channel message, the watch tracks which repeaters re-flooded it. Selecting one of your own outgoing messages opens a **"Heard by"** overlay listing the distinct repeaters that echoed it, along with the signal quality (SNR) heard from each — a quick way to confirm your message is propagating and see which repeaters are carrying it.

On the **S3 Plus** this is reached via the boot button on a selected outgoing message; on the **S3**, via the PWR key.

---

## Notes

A lightweight notes screen stores `.txt` files on the internal LittleFS partition (there is no SD card). Tap while editing to open the touch keyboard; long-press to save and exit.

Notes are entered through the touch keyboard, so each note is capped at **133 characters** — the same length as a standard MeshCore message.

---

## Discovery & Last Heard

- **Discovery** runs an active scan for nearby nodes. Swipe to scroll results, tap to add a node to contacts, long-press to rescan.
- **Last Heard** is a passive advert history — a running list of every node the watch has heard, even long after the advert arrived. Tap an entry to add it to contacts (or remove it). This is especially useful when auto-add is off, as it gives you a catalogue of everything heard on the network.

---

## Rx Log

The **Rx Log** is a packet sniffer, opened from **Settings → Rx Log**. It captures every packet the radio receives — including foreign relays the watch isn't the destination for — and shows each as a block with the route and payload type, time, source/destination or channel, decoded text (for readable channels), and SNR.

- It holds the most recent **100 packets** in a ring buffer, newest shown first; older entries roll off as new ones arrive.
- The log is **held in RAM (PSRAM) only and does not persist across reboots** — it starts empty on every boot.
- Because the watch has no scroll key, the Rx Log **auto-scrolls**, cycling through the captured entries on its own, and new packets appear as they come in.

---

## Repeater Admin & Path Editor

Long-pressing a repeater or room-server contact opens the **repeater admin** login. Enter the admin password via the touch keyboard to manage the repeater over RF.

The **path editor** lets you set a manual repeater chain for a contact, for direct routing. Open it from the Contacts screen using the button (short press on the S3, boot button on the Plus) with a contact selected.

---

## Trace Route

The **Trace** tile runs a trace-route across the mesh — pick a contact or type a path, and the watch reports each hop back. Useful for diagnosing routing and confirming which repeaters a path traverses.

---

## Games

On the **S3**, the **Games** tile (in the slot the Plus uses for Maps) opens a small games menu with **Snake** and **Minesweeper**, both playable by touch.

**Snake** is steered entirely by tapping:

- **Tap anywhere** to start the game, and to restart after a game over.
- **While playing**, tap a zone relative to the centre of the board to steer: tap right of centre to go right, left to go left, above centre to go up, below to go down. The tap direction can't reverse the snake straight back on itself.
- **Tap the top-right corner** to exit back to the games menu, the same back gesture used across the watch UI.

---

## Maps (S3 Plus only)

The S3 Plus has an onboard MIA-M10Q GNSS receiver, so it carries the **Maps** tile — plotting contacts with known GPS positions on a scrollable map. The S3 has no GPS and shows the Games tile in that slot instead.

Map tiles are read from the watch's internal LittleFS partition — a dedicated **~9.9 MB `maps` partition** (mounted at `/maps`), so you can hold a useful subset of your local geography across a few zoom levels. Tiles use the **standard OpenStreetMap "slippy map" layout, exactly the same as Ripple and every other MeshCore device**: PNG tiles at `/tiles/{z}/{x}/{y}.png` (on-disk, `/maps/tiles/{z}/{x}/{y}.png`). A `tiles` folder you've already prepared for any other MeshCore device drops straight in — no watch-specific conversion needed.

To load them, copy the prepared `tiles` folder onto the partition over USB using the browser-based **ESPConnect** tool: connect the watch, open the **LittleFS Tools** tab, drag-and-drop the folder, then write it back with **Save to Flash**. Scott Powell's [T-Watch S3 Plus write-up](https://blog.meshcore.io/2026/03/28/twatch-s3-plus) walks through the ESPConnect steps in detail (see its "Copying Files to LittleFS" section).

> That article covers Ripple's own T-Watch firmware, so only the **ESPConnect / LittleFS file-copy procedure** and the standard tile layout apply to Meck — the firmware variants, watchfaces, partition sizes, and navigation it describes are Ripple-specific and do not match this build.

---

## Settings

Open **Settings** from its home tile. The watch settings screen exposes:

- **Node name**
- **Radio preset**, and the individual radio parameters — **frequency, bandwidth, spreading factor, coding rate, TX power**
- **UTC offset** (see [Clock & Timezone](#clock--timezone))
- **GPS baud** (S3 Plus only)
- **Path hash size** (see [Path Hash Mode](#path-hash-mode))
- **Default region scope** (see [Region Scope](#region-scope))
- **Channels** submenu — add, edit, and manage channels including private ones (see [Channels](#channels-public-private--sharing))

| Gesture | Action |
|---------|--------|
| Swipe up / down | Scroll settings |
| Swipe left / right | Adjust the selected value |
| Tap | Toggle or edit the selected setting |

> Some options present on the larger Meck devices are intentionally not shown on the watch — dark mode, font-style selection, and the large-font toggle are omitted, and the SD-dependent config export/import and WiFi options do not apply.

---

## Path Hash Mode

Meck supports multibyte path hash, in line with MeshCore v1.14. The path hash controls how many bytes each repeater uses to identify itself in forwarded flood packets. Larger hashes reduce the chance of identity collisions at the cost of fewer maximum hops per packet.

Set it from **Settings → Path Hash Size**, or via USB serial:

```
set path.hash.mode 1
```

| Mode | Bytes per hop | Max hops | Notes |
|------|--------------|----------|-------|
| 0 | 1 | 64 | Legacy — prone to hash collisions in larger networks |
| 1 | 2 | 32 | Recommended — effectively eliminates collisions |
| 2 | 3 | 21 | Maximum precision, rarely needed |

Nodes with different path hash modes can coexist on the same network — the hash size is encoded in each packet's header, so receiving nodes adapt automatically. The mode only affects packets your node originates.

---

## Region Scope

Regions limit how far your flood messages propagate through the mesh. When you set a region, outgoing messages are tagged with a transport code that repeaters use to decide whether to forward them. Messages sent without a region reach all repeaters via the default wildcard.

Meck does not pre-set any region on a fresh flash. Region names are determined by your local mesh community — check with your local group for the names in use. Common patterns follow ISO 3166 country/subdivision codes (e.g. `au`, `gb-eng`, `us-ca`), but communities may use custom names. Region names must be lowercase alphanumeric and hyphens only, max 29 characters.

- **Device-wide default:** Set in **Settings → Default Region** (default scope). Applies to all channels and DMs unless a channel overrides it.
- **Per-channel:** In the Channels submenu, a channel can carry its own scope, overriding the device default for that channel only.

---

## Channels: Public, Private & Sharing

Channels are managed from **Settings → Channels**. You can add a public channel (a plain `#name`) or a **private channel** with a randomly generated secret, and edit or delete existing ones. `MAX_GROUP_CHANNELS` is 40 on the watch builds.

<<<<<<< HEAD
| Variant | Environment | BLE | WiFi | 4G Modem | Audio DAC | Web Reader | Max Contacts |
|---------|------------|-----|------|----------|-----------|------------|-------------|
| Audio + BLE | `meck_audio_ble` | Yes | Yes (web reader only) | — | PCM5102A | Yes | 2,000 |
| Audio + WiFi | `meck_audio_wifi` | — | Yes (TCP:5000) | — | PCM5102A | Yes | 2,000 |
| Audio + Standalone | `meck_audio_standalone` | — | — | — | PCM5102A | No | 2,000 |
| 4G + BLE | `meck_4g_ble` | Yes | Yes | A7682E | — | Yes | 2,000 |
| 4G + WiFi | `meck_4g_wifi` | — | Yes (TCP:5000) | A7682E | — | Yes | 2,000 |
| 4G + Standalone | `meck_4g_standalone` | — | Yes | A7682E | — | Yes | 2,000 |
| Remote Repeater (4G) | `meck_remote_repeater` | — | — | A7682E (MQTT) | — | No | — |
| WiFi Repeater | `meck_wifi_repeater` | — | Yes (MQTT) | — | — | No | — |

The audio DAC and 4G modem occupy the same hardware slot and are mutually exclusive. (The T-Deck Max lifts this restriction — it runs both at once. See [T-Deck Max](#t-deck-pro-max).) The remote repeater and WiFi repeater variants operate as dedicated MeshCore repeaters — they forward mesh traffic and respond to guest logins as normal, but **admin management is handled remotely via MQTT** through the [Meck-Mycelium dashboard](https://pelgraine.github.io/Meck-Mycelium), not via the standard mesh admin password login. See [Remote Repeater](#remote-repeater-t-deck-pro-4g) and [WiFi Repeater](#wifi-repeater) below.

### T-Deck Pro Keyboard Controls

The T-Deck Pro firmware includes full keyboard support for standalone messaging without a phone.

### Navigation (Home Screen)

| Key | Action |
|-----|--------|
| W / A | Previous page |
| D | Next page |
| Enter | Select / Confirm |
| M | Open channel picker |
| C | Open contacts list |
| E | Open e-book reader |
| N | Open notes |
| S | Open settings |
| B | Open web browser (BLE and 4G variants only) |
| T | Open SMS & Phone app (4G variant only) |
| P | Open audiobook player (audio variant only) |
| K | Open alarm clock (audio variant only) |
| F | Open node discovery (search for nearby repeaters/nodes) |
| H | Open last heard list (passive advert history) |
| R | Open trace route screen (v1.9+) -- see [Trace Route Screen](#trace-route-screen-v19) |
| J | Open games menu (v1.10+) -- see [Games](#games-v110) |
| G | Open map screen (shows contacts with GPS positions) |
| Mic | Open voice messages (audio variant only) |
| Q | Back to home screen |
| Double-click Boot | Lock / unlock screen |

### Bluetooth (BLE)

BLE is **disabled by default** at boot to support standalone-first operation. The device is fully functional without a phone — you can send and receive messages, browse contacts, read e-books, and set your timezone directly from the keyboard.

To connect to the MeshCore companion app, navigate to the **Bluetooth** home page (use D to page through) and press **Enter** to toggle BLE on. The BLE PIN will be displayed on screen. Toggle it off again the same way when you're done.

### WiFi Companion

The WiFi companion variants (`meck_audio_wifi`, `meck_4g_wifi`) connect to the MeshCore web app, meshcore.js, or Python CLI over your local network via TCP on port 5000. WiFi credentials are stored on the SD card at `/web/wifi.cfg`.

**Connecting:**

1. Navigate to the **WiFi** home page (use D to page through)
2. Press **Enter** to toggle WiFi on
3. The device scans for networks — select yours and enter the password
4. Once connected, the IP address is displayed on the WiFi home page

Connect the MeshCore web app or meshcore.js to `<device IP>:5000`.

WiFi is also used by the web reader and IRC client on WiFi variants. The web reader shares the same connection — no extra setup needed.

### Clock & Timezone

The T-Deck Pro does not include a dedicated RTC chip, so after each reboot the device clock starts unset. The clock will appear in the nav bar (between node name and battery) once the time has been synced by one of these methods:

1. **GPS fix** (standalone) — Once the GPS acquires a satellite fix, the time is automatically synced from the NMEA data. No phone or BLE connection required. Typical time to first fix is 30–90 seconds outdoors with clear sky.
2. **BLE/WiFi companion app** — If connected to the MeshCore companion app (via BLE or WiFi), the app will push the current time to the device.

**Setting your timezone:**

The UTC offset can be set from the **Settings** screen (press **S** from the home screen), or from the **GPS** home page by pressing **U** to open the UTC offset editor.

| Key | Action |
|-----|--------|
| W | Increase offset (+1 hour) |
| S | Decrease offset (-1 hour) |
| Enter | Save and exit |
| Q | Cancel and exit |

The UTC offset is persisted to flash and survives reboots — you only need to set it once. The valid range is UTC-12 to UTC+14. For example, AEST is UTC+10 and AEDT is UTC+11.

The GPS page also shows the current time, satellite count, position, altitude, and your configured UTC offset for reference.

### Channel Message Screen

| Key | Action |
|-----|--------|
| W / S | Scroll messages up/down |
| Enter | Compose new message |
| R | Reply to a message — enter reply select mode, scroll to a message with W/S, then press Enter to compose a reply with an @mention |
| V | View relay path of the last received message (scrollable, up to 20 hops) |
| Q | Back to channel picker |

### Channel Picker

Pressing **M** from the home screen opens the channel picker. All your channels and the DM inbox are shown in a single view with unread message badges, letting you jump directly to any channel instead of cycling through them one at a time. Pressing **Q** from a channel view (e.g. the Public feed) also returns to the channel picker.

| Key | Action |
|-----|--------|
| W / S | Navigate up / down |
| Enter | Switch to selected channel |
| X | Delete message history for highlighted channel (v1.10+) |
| Q | Back to home screen |

Pressing **X** on any highlighted channel brings up a confirmation overlay. Press **Enter** to confirm deletion or **Q** to cancel. This clears all stored messages for that channel from the circular buffer and saves to SD. The channel itself is not removed -- only its message history.

On the T5S3, swiping left or right on the channel messages screen also opens the channel picker, which displays a **vertical bubble list** matching the Meck P4 aesthetic. Long-press a channel to bring up the delete history confirmation.

### Contacts Screen

Press **C** from the home screen to open the contacts list. All known mesh contacts are shown sorted by most recently heard, with their type prefix, estimated hop count, and time since last advert.

**Contact type prefixes**

| Prefix | Type |
|--------|------|
| C | Chat node |
| R | Repeater |
| RS | Room server |
| ? | Unknown / sensor |

**Hop count display**

| Display | Meaning |
|---------|---------|
| `D` | Direct path known (path exchange completed) |
| `D*` | Direct path, manually locked |
| `N` | N-hop path known (e.g. `2` = 2 hops) |
| `N*` | N-hop path, manually locked |
| `~D` | Heard direct via flood advert (no path exchange yet) |
| `~N` | Estimated N hops via flood advert |
| `?` | No path information available |

Flood-based hop estimates (`~D`, `~N`) are drawn from a cache of up to 1,000 recently heard adverts and reset to `?` on reboot until each contact re-advertises. Confirmed path values (`D`, `N`) persist until overwritten by a new path exchange.

**Normal mode controls**

| Key | Action |
|-----|--------|
| W / S | Scroll up / down through contacts |
| Shift+W / Shift+S | Page up / page down |
| A / D | Cycle filter: All → Chat → Rptr → Room → Sens → Fav |
| Enter | Enter select mode (highlights current contact, enables batch operations) |
| P | Open path editor for the highlighted contact |
| Q | Back to home screen |

> **Note:** The **Fav** filter shows only contacts you have marked as favourites. If it appears empty, no contacts have been favourited yet — use select mode (Enter) and then **F** to mark contacts.

**Select mode** — press Enter from the contacts list to enter select mode. The highlighted contact is pre-selected. Use W/S to scroll and Enter to toggle selection on any row.

| Key | Action |
|-----|--------|
| W / S | Scroll up / down |
| Enter | Toggle selection on current contact |
| A | Select all contacts in current filter |
| D | Deselect all |
| F | Toggle favourite on all selected contacts |
| X | Export selected contacts to SD card |
| Backspace | Delete selected contacts |
| Q | Exit select mode |

**Adding contacts**

Contacts can be added three ways:

1. **Automatic** — if Settings → Contacts → Add Mode is set to *Auto All*, any node whose advert is heard is added automatically. *Custom* mode adds only nodes matching the enabled type toggles (Companion, Repeater, Room Server, Sensor) — each toggle controls whether receiving an advert of that type triggers an auto-add. *Manual Only* disables all auto-add.

2. **From the Last Heard screen** — press **H** from the home screen to open the last-heard advert list. Scroll to the node you want and press **Enter** (or tap the row) to add it to contacts. Press **Enter** again on an existing contact to remove it (favourites require a second press within 3 seconds to confirm). Entries show `[+]` if already in contacts, `[★]` if a favourite.

   > **Note:** The Last Heard list holds up to 1,000 entries in PSRAM, and advert data is stored persistently on the SD card — so contacts can be added long after the original advertisement was received, even across reboots. This makes Last Heard especially useful when auto-add is set to *Manual Only*, as it provides a passive catalogue of every node heard on the network.

3. **From the Discovery screen** — press **F** from the home screen to run an active discovery scan. Nodes that respond appear in a list; press **Enter** on any entry to add it to contacts.

**Deleting contacts**

Enter select mode (Enter), select the contacts to remove (Enter to toggle, A to select all), then press **Backspace** to delete. You will be returned to the contacts list once the deletion is complete.

**Exporting and importing contacts**

In select mode, press **X** to export. If contacts are selected, only those contacts are exported; if none are selected, all contacts are exported. Contacts are saved as a JSON file to `/meshcore/` on the SD card with a timestamp in the filename. The JSON format is compatible with MeshCore companion apps — you can copy the file from the SD card and import it into the Android, iOS, or web companion app.

Press **R** on the contacts list (outside select mode) to import contacts from a JSON file on the SD card. The most recent export file in `/meshcore/` is used automatically.

**Contact limits:** All variants support up to 2,000 contacts (stored in PSRAM).

### Sending a Direct Message

Select a **Chat** contact in the contacts list and press **Enter** to start composing a direct message. The compose screen will show `DM: ContactName` in the header. Type your message and press **Enter** to send. The DM is sent encrypted directly to that contact (or flooded if no direct path is known). After sending or cancelling, you're returned to the contacts list.

Contacts with unread direct messages show a `*` marker next to their name in the contacts list.

**Reading received DMs:** From the home screen, press **M** to open the channel picker, then select the **DM Inbox** entry to view received direct messages. This shows all received direct messages with sender name and timestamp. Entering the DM inbox marks all DM messages as read and clears the unread indicator.

### Roomservers

Room servers are MeshCore nodes that host persistent chat rooms. Messages sent to a room server are stored and relayed to anyone who logs in. In Meck, room server messages arrive as contact messages and appear in the DM inbox alongside regular direct messages.

To interact with a room server, navigate to the Contacts screen, filter to **Room** contacts, select the room, and press **Enter** to open the Repeater Admin screen. Log in with the room's admin password to access room administration. On successful login, all unread messages from that room are automatically marked as read.

Room server messages are also synced to the companion app when connected via BLE or WiFi — the companion app will pull and display them alongside other messages.

### Repeater Admin Screen

Select a **Repeater** contact in the contacts list and press **Enter** to open the repeater admin screen. You'll be prompted for the repeater's admin password. Characters briefly appear as you type them before being masked, making it easier to enter symbols and numbers on the T-Deck Pro keyboard.

After a successful login, you'll see a menu with the following remote administration commands:

| Menu Item | Description |
|-----------|-------------|
| Clock Sync | Push your device's clock time to the repeater |
| Send Advert | Trigger the repeater to broadcast an advertisement |
| Neighbors | View other repeaters heard via zero-hop adverts |
| Get Clock | Read the repeater's current clock value |
| Version | Query the repeater's firmware version |
| Get Status | Retrieve repeater status information |

| Key | Action |
|-----|--------|
| W / S | Navigate menu items |
| Enter | Execute selected command |
| Q | Back to contacts (from menu) or cancel login |

Command responses are displayed in a scrollable view. Use **W / S** to scroll long responses and **Q** to return to the menu.

### Trace Route Screen (v1.9+)

The trace route screen lets you build a chain of repeaters and run a trace through them — the same feature available in the MeshCore companion app, but on-device. Each repeater in the chain that recognises its hash appends its receive SNR before forwarding the packet, giving you per-hop signal quality data for the whole route.

Press **R** from the home screen to open the trace screen. T5S3 users with a CardKB attached can use **R** as well.

**Building the path**

There are two ways to add hops to the path:

- **+ Add repeater** — opens a picker showing all known Repeater contacts. Press W/S to scroll, Enter to append the highlighted repeater to the path. The contact picker uses the repeater's stored public key bytes directly, so the hash is always correct regardless of mode.
- **Type Path** — opens an inline text editor for comma-separated decimal hash values, matching the format the companion app uses (e.g. `3601,2198,1244,2198,3601`). Useful when the repeater isn't in your contacts but you know its hash from the app or a community listing.

The path is shown as a numbered list below the menu. Individual hops can be edited or removed, and the whole path can be cleared if you need to start over.

**Hash mode**

Meck supports both 1-byte and 2-byte hash modes. The screen defaults to your device's `path.hash.mode` setting but can be toggled on the fly:

- 1-byte mode (`Mode: 1-byte`) — older networks, more collision-prone but smaller packets
- 2-byte mode (`Mode: 2-byte`) — current MeshCore default in most regions, fewer hash collisions

Use **A / D** on the mode row to toggle, or Enter to cycle.

**Running a trace**

Once at least one hop is in the path, scroll to **Run Trace** and press Enter. Meck creates a `PAYLOAD_TYPE_TRACE` packet and sends it direct-routed through the chain. The screen switches to a "Tracing..." state with an elapsed-time counter while waiting for the response.

When the trace completes, the screen shows:

- Each hop in order with its receive SNR (in dB)
- The final SNR of the response packet arriving back at your device
- Total round-trip time in milliseconds

If 30 seconds pass without a response, the trace times out and the screen returns to the build view so you can adjust the path and try again.

**Symmetric paths and direct visibility**

For a round-trip trace, the path needs to be symmetric. To trace through repeaters A → B → C and back, type `A,B,C,B,A`. The trace packet travels out through A→B→C, and the response comes back C→B→A.

You also need to be able to **hear the last repeater in the chain directly** — that's the one that sends the response back to your device. If the last hop is too far away to hear, the trace will time out even though the outbound leg succeeded.

| Key | Action |
|-----|--------|
| W / S | Navigate menu items |
| A / D | Toggle hash mode (1-byte / 2-byte) on the mode row |
| Enter | Select / confirm / open editor |
| 0–9 , | Path values (when the inline text editor is open) |
| Q | Cancel edit / back to home screen |

The screen supports up to **16 hops** per trace.

### Rx Log

The Rx Log is an on-device packet sniffer, mirroring the Rx Log in the MeshCore companion app. Open it from **Settings -> Rx Log >>**. It captures every packet the radio receives -- including relays destined for other nodes, since capture happens before filtering -- into a buffer of the most recent 100 packets, shown newest first.

Each entry shows the route type (flood or direct) and payload type, the receive time and wire size, the packet hash, the hop path, and the channel hash/name (for group messages) or the From/To node hashes (for addressed packets). For channel messages your device can decrypt, the decoded "sender: message" line is attached as well.

| Key | Action |
|-----|--------|
| W / S | Scroll through entries (newest at the top) |
| Q | Back to settings |

The log is held in RAM only and is cleared on reboot.

A running **RX packets** count also appears on the radio details page on the home screen (page through with **D**), just beneath the noise-floor reading. It counts flood and direct packets received since boot and resets on reboot or whenever you change radio parameters (frequency, bandwidth, or spreading factor).

### Delete Message History (v1.10+)

You can clear all stored messages for any individual channel or the DM inbox without removing the channel itself.

From the home screen, press **M** to open the channel picker. Navigate to the channel you want to clear and press **X**. A confirmation overlay appears asking "Delete message history?" -- press **Enter** to confirm or **Q** to cancel.

On the T5S3, long-press the channel in the channel picker to bring up the same confirmation. Tap to confirm or press the Boot button to cancel.

Messages are invalidated in the circular buffer and the change is saved to SD immediately. The unread counter is also reset. New messages will continue to appear as they arrive.

### Per-Channel Notification Preferences (v1.10+)

Each channel (and the DM inbox) can be individually set to one of three notification levels:

- **All** -- notify on every message (default)
- **@ (Mentions)** -- only notify when someone tags you with @YourNodeName or @[YourNodeName]
- **Off** -- completely muted (no buzzer, no keyboard flash, no screen wake, no toast)

Messages are always stored in history regardless of the notification setting -- only the alerts and unread badges are suppressed.

To change a channel's notification preference: from the home screen, press **S** to open settings, scroll down to the **Channels >>** section and open it. Navigate to the channel you want to configure, and press **N** to cycle through the three modes. The current setting is shown in the channel row hint as `N:All`, `N:@`, or `N:Off`.

### Custom Notification Tones (v1.10+)

Each channel can have its own notification tone instead of the default buzzer sound. When a message arrives on a channel with a custom tone assigned, that tone plays through the speaker instead of the RTTTL buzzer.

To assign a tone: from the home screen, press **S** to open settings, scroll down to the **Channels >>** section and open it. Navigate to the channel you want, and press **T**. The tone picker appears with a list of available sounds. Use **W/S** to browse, **Enter** to select, or **Q** to cancel. Select "Default (silent)" to remove a custom tone and revert to the standard buzzer.

**Audio variant (PCM5102A DAC):** A selection of bundled tones are copied to the `/alarms/` folder on the SD card on first boot. You can also add your own MP3 files to that folder -- they'll appear in the tone picker alongside the bundled options. This gives you complete flexibility to use any short MP3 as a notification sound.

**4G variant (A7682E modem):** Seven bundled notification tones are embedded in the firmware as 8kHz mono WAV files and transferred to the modem's internal filesystem on boot. Playback goes through the modem's own speaker amplifier via AT+CCMXPLAY. Custom user-supplied tones are not supported on the 4G variant -- only the bundled set is available.

**Available bundled tones:** Bell, Ding, High Trill, Low Soft Ding (x2), Mid Trill, and Soft Notif. All are short, 1-2 second alert sounds.

**T-Deck Max -- Buzzer (vibrate):** On the MAX, the tone picker includes an extra **Buzzer (vibrate)** option above the sound files. When selected, an incoming message on that channel pulses the MAX's DRV2605 haptic motor instead of playing a tone or the RTTTL buzzer -- useful for silent alerts. Because the MAX has its own ES8311 audio codec, it also supports custom MP3 tones from the `/alarms/` folder, exactly like the T-Deck Pro audio variant (and unlike the T-Deck Pro 4G variant). See [Buzzer (Vibrate) Notifications](#buzzer-vibrate-notifications).

### Games (v1.10+)

Press **J** from the home screen to open the games menu. Two classic games are included:

**Snake** -- the Nokia classic. Guide the snake around the screen to eat food and grow longer without crashing into the walls or your own tail. The game runs on the e-ink display at a pace suited to the refresh rate.

| Key | Action |
|-----|--------|
| W | Turn up |
| A | Turn left |
| S | Turn down |
| D | Turn right |
| Q | Quit to games menu |

**Minesweeper** -- clear the board without hitting a mine. Numbers reveal how many adjacent cells contain mines. Flag cells you suspect are mines to keep track.

| Key | Action |
|-----|--------|
| W / A / S / D | Move cursor |
| Enter | Reveal cell |
| F | Toggle flag on cell |
| Q | Quit to games menu |

On the T-Deck Max the board is larger, with a 15x20 grid and 50 mines.

### Private Channels (v1.11+)

Meck supports both public hashtag channels and private channels. The difference is in how the channel secret is generated:

- **Public (hashtag) channels** — type a name starting with `#` (e.g. `#camping`). The 16-byte secret is derived deterministically from the name via SHA-256, so anyone on any MeshCore device who creates the same hashtag name gets the same key and can communicate on that channel.
- **Private channels** — type a name **without** the `#` prefix (e.g. `team-alpha`). A cryptographically random 16-byte secret is generated, meaning only devices that have been explicitly given the key can participate.

**How to create a private channel:**

1. From the home screen, press **S** to open settings
2. Scroll down to the **Channels >>** section and press Enter to open it
3. Navigate to **+ Add Channel (# = public)** and press Enter
4. Type the channel name **without** a `#` prefix, then press Enter

The channel is created with a random secret. To let other Meck users join, use channel sharing (see below).

### Channel Sharing via DM (v1.11+)

You can share any channel — public or private — with another Meck user by sending them the channel name and secret as an encrypted direct message. This is particularly useful for private channels, where sharing the secret manually would mean typing 32 hex characters.

**How to share a channel:**

1. From the home screen, press **S** to open settings
2. Scroll down to the **Channels >>** section and press Enter to open it
3. Navigate to the channel you want to share
4. Press **C** to open the contact picker
5. Select a contact and press Enter to send

The recipient's device automatically adds the channel to their channel list (if it doesn't already exist and there's an empty slot). An alert confirms the channel was added. In the DM conversation, both sender and recipient see a sanitised message ("Shared channel: name") rather than the raw protocol data.

On the T5S3, channel sharing works the same way via the CardKB keyboard.

### Config Export/Import (v1.11+)

The **Export/Import >>** sub-screen in Settings lets you back up and restore your device configuration via JSON files on the SD card. This is useful for migrating settings to a new board, keeping a backup before reflashing, or cloning a configuration across multiple devices.

**Exporting:**

1. From the home screen, press **S** to open settings
2. Scroll down to **Export/Import >>** and press Enter
3. Select **Export to SD >>** and press Enter
4. Toggle which sections to include using Enter on each checkbox:
   - **Identity** — public and private key (full identity backup)
   - **Radio Settings** — frequency, bandwidth, spreading factor, coding rate, TX power, and GPS position
   - **Channels** — all channel names and secrets
   - **Contacts** — all contacts with public keys, type, flags, position, and timestamps
   - **Auto-Add Prefs** — contact auto-add mode and per-type toggles
5. Navigate to **>> Export Now** and press Enter

The config is saved as a timestamped JSON file in `/meshcore/` on the SD card (e.g. `meshcore_config_20260523_1430.json`). The format is compatible with the MeshCore companion app config export.

**Importing:**

Place a file named `import.json` in the `/meshcore/` folder on your SD card. Then either:

- **From settings:** go to **Export/Import >> → Import from SD** and press Enter
- **On boot:** the firmware automatically checks for `/meshcore/import.json` at startup and imports it if found

If the import includes a different identity (private key), the device reboots after applying the new identity. Channels and contacts are merged into the existing configuration.

### Settings Screen

Press **S** from the home screen to open settings. On first boot (when the device name is still the default hex ID), the settings screen launches automatically as an onboarding wizard to set your device name and radio preset.

| Key | Action |
|-----|--------|
| W / S | Navigate up / down through settings |
| Shift+W / Shift+S | Page up / page down |
| Enter | Edit selected setting, or enter a sub-screen |
| Q | Back one level (sub-screen → top level → home screen) |

**Available settings:**

| Setting | Edit Method |
|---------|-------------|
| Device Name | Text entry — type a name, Enter to confirm |
| Radio Preset | A / D to cycle presets (MeshCore Default, Long Range, Fast/Short, EU Default), Enter to apply |
| Frequency | Text entry — type exact value (e.g. 916.575), Enter to confirm |
| Bandwidth | W / S to cycle standard values (31.25 / 62.5 / 125 / 250 / 500 kHz), Enter to confirm |
| Spreading Factor | W / S to adjust (5–12), Enter to confirm |
| Coding Rate | W / S to adjust (5–8), Enter to confirm |
| TX Power | W / S to adjust (1–20 dBm), Enter to confirm |
| UTC Offset | W / S to adjust (-12 to +14), Enter to confirm |
| Msg Rcvd LED Pulse | Toggle keyboard backlight flash on new message (Enter to toggle) |
| GPS Baud Rate | A / D to cycle (Default 38400 / 4800 / 9600 / 19200 / 38400 / 57600 / 115200), Enter to confirm. **Requires reboot to take effect.** |
| Path Hash Mode | W / S to cycle (1-byte / 2-byte / 3-byte), Enter to confirm |
| Default Region | Text entry — type a region name (e.g. `au-nsw`), Enter to confirm. Empty = unscoped. See [Region Scope](#region-scope-v17). |
| Dark Mode | Toggle inverted display — white text on black background (Enter to toggle) |
| Larger Font | Toggle larger text size on channel messages, contacts, DM inbox, and repeater admin screens (Enter to toggle) |
| Font Style | A / D to cycle styles (Classic / Noto Sans / Montserrat), Enter to apply. See [Font Styles](#font-styles). |
| Auto Lock | A / D to cycle timeout (None / 2 / 5 / 10 / 15 / 30 min), Enter to confirm |
| Contacts >> | Opens the Contacts sub-screen (see below) |
| Channels >> | Opens the Channels sub-screen (see below) |
| OTA Tools >> | Opens the OTA sub-screen — Firmware Update and SD File Manager (see [OTA Firmware Update](#ota-firmware-update-v13)) |
| Export/Import >> | Opens the Export/Import sub-screen — export device config to SD or import from `/meshcore/import.json` (see [Config Export/Import](#config-exportimport-v111)) |
| Rx Log >> | Opens the Rx Log packet sniffer (see [Rx Log](#rx-log)) |
| Device Info | Public key and firmware version (read-only) |

**Contacts sub-screen** — press Enter on the `Contacts >>` row to open. Contains the contact auto-add mode picker and, when set to Custom, per-type toggles:

| Toggle | Meaning when ON |
|--------|----------------|
| Companion | Auto-add a chat node when its advert is heard |
| Repeater | Auto-add repeaters heard via advert |
| Room Server | Auto-add room servers heard via advert |
| Sensor | Auto-add sensor nodes heard via advert |
| Overwrite Oldest | When the contact list is full, overwrite the oldest non-favourite entry instead of discarding the new contact |

Press Q to return to the top-level settings list.

**Channels sub-screen** -- press Enter on the `Channels >>` row to open. Lists all current channels with their region scope tags (e.g. `[au-nsw]` or `[*]` for device default). The hint line for each channel shows its current notification preference (`N:All`, `N:@`, or `N:Off`) and available actions.

| Key | Action |
|-----|--------|
| Enter | Edit channel region scope |
| N | Cycle notification preference (All / Mentions / Off) -- see [Per-Channel Notification Preferences](#per-channel-notification-preferences-v110) |
| T | Open notification tone picker (audio and 4G variants) -- see [Custom Notification Tones](#custom-notification-tones-v110) |
| C | Share channel with a contact via DM -- see [Channel Sharing via DM](#channel-sharing-via-dm-v111) |
| X | Delete channel (non-primary channels only) |
| Q | Back to top-level settings |

The top-level settings screen also displays your node ID and firmware version. On the 4G variant, IMEI, carrier name, and APN details are shown here as well.

When adding a channel, type the channel name and press Enter. Names starting with `#` create a public hashtag channel (secret derived from the name via SHA-256, matching the standard MeshCore convention). Names without a `#` prefix create a private channel with a random secret — see [Private Channels](#private-channels-v111).

If you've changed radio parameters, pressing Q will prompt you to apply changes before exiting.

> **Tip:** All device settings (plus mesh tuning parameters not available on-screen) can also be configured via USB serial. See the [Serial Settings Guide](Serial_Settings_Guide.md) for complete documentation.

### Font Styles

Meck supports three font styles across the entire UI: **Classic** (the original FreeSans look), **Noto Sans** (clean, excellent Latin Extended coverage), and **Montserrat** (geometric, distinctive).

Change the font in **Settings → Font** — use A/D to cycle with a live preview, then Enter to apply. Press Q to cancel and revert. The selected style applies to all screens including channel messages, contacts, settings, and the e-reader.

Font styles are available in both Tiny and Larger text size modes. Custom fonts at Tiny size use 7pt glyphs; at Larger size, 9pt — matching the existing FreeSans layout. The font preference is saved and persists across reboots.

**Accented character support (v1.8+):** All three font styles now display accented and diacritical characters (Czech, Polish, French, German, etc.) instead of dropping them. **Noto Sans at Larger text size** renders diacritical marks natively (carons, accents, cedillas). All other font and size combinations fold accented characters to their ASCII base letter (ě→e, ž→z, ñ→n) — the letter is always visible, just without the diacritic mark.

### Compose Mode

| Key | Action |
|-----|--------|
| Enter | Send message |
| Backspace | Delete last character |
| Shift + Backspace | Cancel and exit compose mode |

### Symbol Entry (Sym Key)

Press the **Sym** key then the letter key to enter numbers and symbols:

| Key | Sym+ | | Key | Sym+ | | Key | Sym+ |
|-----|------|-|-----|------|-|-----|------|
| Q | # | | A | * | | Z | 7 |
| W | 1 | | S | 4 | | X | 8 |
| E | 2 | | D | 5 | | C | 9 |
| R | 3 | | F | 6 | | V | ? |
| T | ( | | G | / | | B | ! |
| Y | ) | | H | : | | N | , |
| U | _ | | J | ; | | M | . |
| I | - | | K | ' | | Mic | 0 |
| O | + | | L | " | | $ | Emoji picker (Sym+$ for literal $) |
| P | @ | | | | | | |

### Other Keys

| Key | Action |
|-----|--------|
| Shift | Uppercase next letter |
| Alt | Same as Sym (for numbers/symbols) |
| Space | Space character / Next in navigation |

### Emoji Picker

While in compose mode, press the **$** key to open the emoji picker. A scrollable grid of 79 emoji is displayed in a 5-column layout, with faces and emotions grouped first. Scrolling wraps around — pressing W on the first row goes to the last row and vice versa.

| Key | Action |
|-----|--------|
| W / S | Navigate up / down |
| A / D | Navigate left / right |
| Enter | Insert selected emoji |
| $ / Q / Backspace | Cancel and return to compose |

### SMS & Phone App (Pro 4G * Max only)

Press **T** from the home screen to open the SMS & Phone app. The app opens to a menu screen where you can choose between the **Phone** dialer (for calling any number) or the **SMS Inbox** (for messaging and calling saved contacts). The SMS Inbox entry shows the number of unread received messages in brackets (e.g. **SMS Inbox [3]**); the badge disappears once everything is read.

<<<<<<< HEAD
**Note for US users: the A7682E modem in the T-Deck Pro and T-Deck Pro Max supports very limited US cellular bands, so 4G is almost entirely unusable with US SIMs. For example, the 4G is wholly unusable on T-Mobile and may have very limited use on AT&T, due to the nature of the A7682E and the bands used by it.**
=======
# Note for US users: the A7682E modem in the T-Deck Pro and T-Deck Pro Max supports very limited US cellular bands, so 4G is almost entirely unusable with US SIMs. For example, the 4G is wholly unusable on T-Mobile and may have very limited use on AT&T, due to the nature of the A7682E and the bands used by it.
>>>>>>> dev

For full documentation including key mappings, dialpad usage, contacts management, and troubleshooting, see the [SMS & Phone App Guide](SMS___Phone_App_Guide.md).

### Web Browser & IRC

Press **B** from the home screen to open the web reader. This is available on the BLE, WiFi, and 4G variants (not the standalone audio variant, which excludes WiFi to preserve lowest-battery-usage design).

The web reader home screen provides access to the **IRC client**, the **URL bar**, and your **bookmarks** and **history**. Select IRC Chat and press Enter to configure and connect to an IRC server. Select the URL bar to enter a web address, or scroll down to open a bookmark or history entry.

The browser is a text-centric reader best suited to text-heavy websites. It also includes basic web search via DuckDuckGo Lite, and can download EPUB files — follow a link to an `.epub` and it will be saved to the books folder on your SD card for reading later in the e-book reader.

For full documentation including key mappings, WiFi setup, bookmarks, IRC configuration, and SD card structure, see the [Web App Guide](Web_App_Guide.md).

### Alarm Clock (Pro Audio & Max only)

Press **K** from the home screen to open the alarm clock. This is available on the T-Deck Pro Audio variant (PCM5102A DAC) and on the T-Deck Max (ES8311 codec). Set up to five daily alarms that play custom MP3 files through the headphone jack.

**Setup:**

1. Place MP3 files (44100 Hz sample rate) in `/alarms/` on the SD card
2. Press **K** to open the alarm clock
3. Select an alarm slot (1–5) with **W / S** and press **Enter** to edit
4. Set the hour and minute, then choose an MP3 file from the list
5. Press **Enter** to save the alarm

| Key | Action |
|-----|--------|
| W / S | Navigate alarm slots / adjust time |
| A / D | Switch between hour and minute fields |
| Enter | Edit slot / save alarm / select MP3 |
| X | Delete selected alarm |
| Q | Back to home screen |

**When an alarm fires:**

The selected MP3 plays through the headphone jack, even if you're on another screen or playing an audiobook.

| Key | Action |
|-----|--------|
| Z | Snooze for 5 minutes |
| Any other key | Dismiss alarm |

Alarm configuration is stored in `/alarms/.alarmcfg` on the SD card. Alarms persist across reboots — if the RTC has valid time (via GPS or companion app sync), alarms fire at the correct time after a restart.

> **Note:** MP3 files should be encoded at **44100 Hz** sample rate. Lower sample rates may cause distortion due to ESP32-S3 I2S hardware limitations (same requirement as the audiobook player).

**SD Card Folder Structure:**

```
SD Card
├── alarms/
│   ├── .alarmcfg             (auto-created, stores alarm slot config)
│   ├── morning-chime.mp3
│   ├── rooster.mp3
│   └── gentle-bells.mp3
├── audiobooks/               (existing — audiobook player)
│   └── ...
├── books/                    (existing — text reader)
│   └── ...
└── ...
```

### Voice Notes Over LoRa (Pro Audio & Max only)

Press the **Microphone key** (the zero key on the keyboard) to open the Voice Messages screen. This is available on the T-Deck Pro Audio variant (PCM5102A DAC) and on the T-Deck Max (ES8311 codec).

Record and send voice messages of up to 12 seconds over LoRa. Audio is encoded on-device using Codec2 at 1200 bps, compressing each second of speech into a single 150-byte LoRa packet. Voice notes use very little airtime relative to what they deliver — a 5-second message is just 5 packets.

Voice notes can be sent to another T-Deck Pro Audio device (plays automatically through the headphone jack) or to any MeshCore companion device connected to the [Meck-Mycelium web app](https://pelgraine.github.io/Meck-Mycelium) (plays through your phone's speaker as a tappable bubble in the DM view).

**Sending a voice note:**

1. Press the **Microphone key** to open the Voice Messages screen
2. Press and **hold** the Microphone key to record — release to stop (max 12 seconds)
3. Press **S** to open the contact picker — contacts with a direct path appear at the top
4. Scroll to your contact and press **Enter** to send

Packets are sent with staggered 3-second delays to avoid congesting the channel. On a 62.5 kHz / SF7 radio preset (e.g. Australia Narrow), a 5-second voice note arrives in roughly 20 seconds and a 12-second recording in about 42 seconds.

**Receiving voice notes:**

* **On a T-Deck Pro Audio device:** the voice message screen opens automatically and the message plays through the headphone jack. **Headphones are recommended** — the built-in speaker is very quiet.
* **Via Meck-Mycelium:** voice messages appear as "🎙️ Voice message" bubbles in the DM view. Tap to play. Codec2 decoding happens entirely in the browser via WebAssembly.

> **Note:** Voice recording and sending requires the **Audio variant** hardware (PCM5102A DAC). Receiving and playback works on all Audio variants. Non-audio Meck devices can receive and relay voice packets but cannot play them locally — use the Meck-Mycelium web app for playback on those devices.

| Key | Action |
|-----|--------|
| Mic (hold) | Record voice note |
| Mic (release) | Stop recording |
| S | Open contact picker to send |
| Enter | Send to selected contact |
| Q | Back to home screen |

### Lock Screen (T-Deck Pro & Max)

Double-click the Boot button to lock the screen. The lock screen shows the current time, battery percentage, and unread message count. The CPU drops to 40 MHz while locked to reduce power consumption.

Double-click the Boot button again to unlock and return to whatever screen you were on.

An auto-lock timer can be configured in **Settings → Auto Lock** (None / 2 / 5 / 10 / 15 / 30 minutes of idle time).

### Shutdown (T-Deck Pro & Max)

The home screen includes a **Shutdown** page. Selecting it powers the device off completely — the ESP32-S3 enters deep sleep with no wake sources, peripheral power is cut, and the LoRa module is powered down. Only a hardware reset (reset button) or USB power-on will wake the device. This is distinct from the auto-lock hibernate, which maintains wake-on-LoRa capability.
=======
Private channels and channel sharing follow the standard Meck/MeshCore model — a private channel is encrypted with its secret, and sharing sends the channel to a contact so they can join it.
>>>>>>> dev

---

## Bluetooth (BLE)

On the **BLE** builds, BLE is disabled by default at boot, consistent with the other Meck devices and standalone-first operation. Enable it to connect the MeshCore companion app or the [Meck-Mycelium web app](https://pelgraine.github.io/Meck-Mycelium). **Standalone** builds omit BLE entirely and run fully self-contained — no phone required for any function.

---

## Battery & Charging

Both watches ship with **LiHV** (high-voltage lithium) cells: 3.8 V nominal, 4.35 V maximum charge. The S3's cell is a 470 mAh Tewaycell 552530; the S3 Plus uses a Tewaycell 112530-2P of the same chemistry. Meck charges both to 4.35 V to use the full capacity of the LiHV cell.

> LilyGo's published battery specifications for these watches (1500 mAh, 3.7 V) do not match the cells actually fitted — the figures above are taken from the physical cell labels. If you replace a cell, confirm its chemistry: charging a standard 3.7 V / 4.2 V-max cell to 4.35 V is unsafe.

Charge over USB-C. The AXP2101 PMU manages charging; a moderate charge current is recommended for cell longevity.

---

## About MeshCore

MeshCore is a lightweight, portable C++ library that enables multi-hop packet routing for embedded projects using LoRa and other packet radios. It is designed for resilient, decentralized communication networks that work without the internet — devices (nodes) relay messages through intermediate nodes to communicate over long distances, which is especially useful in off-grid, emergency, or tactical situations.

Key characteristics:

- **Multi-hop packet routing** — messages are forwarded across nodes, extending range beyond a single radio's reach, with a configurable hop limit. "Companion" nodes (like these watches) do not repeat messages, which keeps routing paths clean.
- **Decentralized & resilient** — no central server or internet required; the network is self-healing.
- **Low power** — suited to battery-powered devices.

Use cases include off-grid communication, emergency response and disaster recovery, outdoor activities, tactical and security applications, and IoT sensor networks.

---

## MeshCore Clients

The watch **BLE** builds can connect to a MeshCore companion client over Bluetooth. (Standalone builds run without a client.) BLE is disabled by default at boot — enable it first.

- Web: https://app.meshcore.nz
- Meck-Mycelium: https://pelgraine.github.io/Meck-Mycelium
- Android: https://play.google.com/store/apps/details?id=com.liamcottle.meshcore.android
- iOS: https://apps.apple.com/us/app/meshcore/id6742354151?platform=iphone
- NodeJS: https://github.com/liamcottle/meshcore.js
- Python: https://github.com/fdlamotte/meshcore-cli

> **Note:** WebBLE (used by the browser clients) requires Chrome or a Chromium-based browser. Safari and Firefox do not support WebBLE.

---

## Contributing

Please submit PRs using `dev` as the base branch. For minor changes just open the PR; for anything more impactful, open an Issue first to discuss the approach — especially where it affects structure or architecture.

General principles:

- Keep it simple — think embedded, not high-level. Concise code, no unnecessary layers.
- No dynamic memory allocation except during setup/begin functions.
- Match the existing brace and indenting style; do not retroactively reformat existing code (it creates noisy diffs).

For developers:

- Install [PlatformIO](https://docs.platformio.org) in [Visual Studio Code](https://code.visualstudio.com).
- Clone and open this repository.
- Build for your target using the environment names in [Build Variants](#build-variants).

## 📞 Get Support

- Join the [MeshCore Discord](https://discord.gg/KWFeY45sN) to chat with the developers and get help from the community.

## 📜 License

The upstream [MeshCore](https://github.com/meshcore-dev/MeshCore) library is released under the **MIT License** (Copyright © 2025 Scott Powell / rippleradios.com). Meck-specific code (UI screens, display helpers, device integration) is also provided under the MIT License.

This firmware links against third-party libraries with their own license terms; review the individual licenses below if you intend to redistribute or modify the firmware.

### Third-Party Libraries

| Library | License | Author / Source |
|---------|---------|-----------------|
| [MeshCore](https://github.com/meshcore-dev/MeshCore) | MIT | Scott Powell / rippleradios.com |
| [RadioLib](https://github.com/jgromes/RadioLib) | MIT | Jan Gromeš |
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | FreeBSD | lovyan03 (watch display) |
| [XPowersLib](https://github.com/lewisxhe/XPowersLib) | MIT | Lewis He (AXP2101 PMU) |
| [SensorLib](https://github.com/lewisxhe/SensorLib) | MIT | Lewis He (BMA423, PCF8563) |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) | BSD | Adafruit |
| [PNGdec](https://github.com/bitbank2/PNGdec) | Apache-2.0 | Larry Bank / bitbank2 (image decoding) |
| [Arduino Crypto](https://github.com/rweather/arduinolibs) | MIT | Rhys Weatherley |
| [base64](https://github.com/Densaugeo/base64_arduino) | MIT | densaugeo |
| [RTClib](https://github.com/adafruit/RTClib) | MIT | Adafruit |
| [Melopero RV3028](https://github.com/melopero/Melopero_RV-3028_Arduino_Library) | MIT | Melopero |
| [CayenneLPP](https://github.com/ElectronicCats/CayenneLPP) | MIT | Electronic Cats |

Full license texts for each dependency are available in their respective repositories linked above.
