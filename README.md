## Meshcore + Fork = Meck (T-Watch Edition)

A fork created specifically to focus on enabling BLE & standalone Meshcore companion firmware for the **LilyGo T-Watch S3** and **T-Watch S3 Plus**. This is the watch-only build of Meck; the multi-device Meck firmware (T-Deck Pro, T-Deck Max, T5S3 E-Paper Pro, Heltec repeaters) lives in the [main Meck repository](https://github.com/pelgraine/Meck). 100% vibecoded. Created with Claude AI on MeshCore v1.11 code.

[Check out the Meck discussion channel on the MeshCore Discord](https://discord.com/channels/1495203904898728149/1496789639556501614)

<img width="240" height="250" alt="IMG_3813" src="https://github.com/user-attachments/assets/291cc13c-b437-48ca-8f7b-26b488494c22" />
<img width="250" height="250" alt="IMG_3814" src="https://github.com/user-attachments/assets/69ad1500-b7a6-4a2e-a13d-9e298f0d7a6a" />
<img width="250" height="250" alt="IMG_3817" src="https://github.com/user-attachments/assets/9b802bce-959a-41e9-81b4-2845a979f6d6" />


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

Inside a channel, scroll messages by swiping, tap the compose bar at the bottom to write a new message via the touch keyboard, and long-press to go back. Replies and @mentions follow the standard MeshCore conventions.

Long messages are truncated to a single line in the list, but they aren't cut off — **tap a message** to display it in ticker-scroll mode, scrolling the full text across the screen from left to right so you can read the whole message. Tap it again to stop the ticker.

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

Channels are managed from **Settings → Channels**. You can add a public channel (a plain `#name`) or a **private channel** with a randomly generated secret, and edit or delete existing ones. `MAX_GROUP_CHANNELS` is 20 on the watch builds, plus the DMs slot.

Private channels and channel sharing follow the standard Meck/MeshCore model — a private channel is encrypted with its secret, and sharing sends the channel to a contact so they can join it.

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