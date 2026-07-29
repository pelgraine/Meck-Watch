## Meshcore + Fork = Meck (T-Watch Edition)

A fork created specifically to focus on enabling BLE, WiFi & standalone Meshcore companion firmware for the **LilyGo T-Watch S3** and **T-Watch S3 Plus**. This is the watch-only build of Meck; the multi-device Meck firmware (T-Deck Pro, T-Deck Max, T5S3 E-Paper Pro, Heltec repeaters) lives in the [main Meck repository](https://github.com/pelgraine/Meck). 100% vibecoded. Created with Claude AI on MeshCore v1.11 code.

[Check out the Meck discussion channel on the MeshCore Discord](https://discord.com/channels/1495203904898728149/1496789639556501614)

<img width="240" height="250" alt="IMG_3813" src="https://github.com/user-attachments/assets/291cc13c-b437-48ca-8f7b-26b488494c22" />
<img width="250" height="250" alt="IMG_3814" src="https://github.com/user-attachments/assets/69ad1500-b7a6-4a2e-a13d-9e298f0d7a6a" />
<img width="250" height="250" alt="IMG_3817" src="https://github.com/user-attachments/assets/9b802bce-959a-41e9-81b4-2845a979f6d6" />


### Contents
- [Supported Devices](#supported-devices)
- [S3 vs S3 Plus at a Glance](#s3-vs-s3-plus-at-a-glance)
- [No SD Card](#no-sd-card)
- [Build Environment Options](#build-environment-options)
- [S3 Plus: No Maps Builds](#s3-plus-no-maps-builds)
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
- [Experimental: RX Duty Cycle](#experimental-rx-duty-cycle)
- [Path Hash Mode](#path-hash-mode)
- [Region Scope](#region-scope)
- [Channels: Public, Private & Sharing](#channels-public-private--sharing)
- [Bluetooth (BLE)](#bluetooth-ble)
- [WiFi Companion](#wifi-companion)
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
| Home tile (row 3, right) | **Games** (Trace + Games) | **Maps** (Trace + Maps); **Games** on the no-maps builds |
| Battery cell | 470 mAh LiHV (Tewaycell 552530, 3.8 V nom / 4.35 V max) | LiHV (Tewaycell 112530-2P, 3.8 V nom / 4.35 V max) |
| Measured runtime | 14h27m (standalone, 40 MHz cap, duty cycle off) | 21h24m (no-maps standalone, 40 MHz cap, GPS off) |
| Firmware version string | `Meck TWatch S3 v1.0` / `Meck TWatch S3 BLE v1.0` / `Meck TWatch S3 WiFi v1.0` | `Meck TWatch v1.0` / `Meck TWatch BLE v1.0` / `Meck TWatch WiFi v1.0` |

Everything else — the UI, the touch keyboard, step counting, tilt-to-wake, the lock/clock screen, notes, and the whole channel/DM/contacts system — is shared between the two.

---

## No SD Card

Unlike the other Meck devices, the watches have **no SD card slot**. All watch-local data — notes, step history, and configuration — is stored on an internal LittleFS partition instead. There is nothing to insert, format, or manage.

Two consequences worth knowing:

- Features that are inherently SD-dependent on other Meck devices — the e-book/EPUB reader, audiobooks, and the web reader — are **not part of the watch build** at all.
- The **config export/import** menu items compile in but write to an SD card, so they are non-functional on the watch. Move config between devices using the companion app or the main-repo devices instead.

---

## Build Environment Options

There are eight build environments -- three for the S3, five for the S3 Plus.

| Variant | Environment | BLE / WiFi | Map screen | CPU cap | Max Contacts |
|---------|------------|-----|-----|-----|-------------|
| S3 Standalone | `meck_twatch_s3_standalone` | -- | n/a (no GPS) | 40 MHz | 2,000 |
| S3 BLE | `meck_twatch_s3_ble` | BLE | n/a (no GPS) | 80 MHz | 2,000 |
| S3 WiFi | `meck_twatch_s3_wifi` | WiFi | n/a (no GPS) | 80 MHz | 2,000 |
| S3 Plus Standalone | `meck_twatch_s3_plus_standalone` | -- | Yes | 80 MHz | 2,000 |
| S3 Plus BLE | `meck_twatch_s3_plus_ble` | BLE | Yes | 80 MHz | 2,000 |
| S3 Plus WiFi | `meck_twatch_s3_plus_wifi` | WiFi | Yes | 80 MHz | 2,000 |
| S3 Plus Standalone, No Maps | `meck_twatch_s3_plus_standalone_nomaps` | -- | No | 40 MHz | 2,000 |
| S3 Plus BLE, No Maps | `meck_twatch_s3_plus_ble_nomaps` | BLE | No | 80 MHz | 2,000 |

**Standalone** builds run the full touch UI with no phone connection -- you can send and receive messages, browse contacts, and manage the mesh entirely from the watch. **BLE** builds add the MeshCore companion serial interface so a phone app can connect when you want it. **WiFi** builds instead expose the MeshCore companion interface over TCP on port 5000, so a client on your local network can connect -- see [WiFi Companion](#wifi-companion).

**No Maps** builds drop the map screen on the S3 Plus in exchange for the best battery life on that watch, and change how the clock face and buttons behave -- see [S3 Plus: No Maps Builds](#s3-plus-no-maps-builds).

All eight builds allocate their 2,000-contact arrays in PSRAM and boot the CPU at 80 MHz (`ESP32_CPU_FREQ=80`). The two standalone builds marked 40 MHz above go further and cap the *running* clock -- both idle and boost -- at 40 MHz. The BLE and WiFi builds are deliberately left at 80 MHz, because WiFi and Bluetooth will not run below it.

### Files in each release

Each environment ships two files: a merged image for a first flash, and a plain `.bin` for updating a watch that already runs Meck. See [Flashing Firmware](#flashing-firmware) for the addresses.

**T-Watch S3**

| File | Description |
|---|---|
| `meck_twatch_s3_ble-merged.bin` | BLE companion build, full-flash image -- recommended first flash for initial setup and backup via the MeshCore app. |
| `meck_twatch_s3_ble.bin` | BLE companion build, for a watch that already runs Meck. Config is kept. |
| `meck_twatch_s3_wifi-merged.bin` | WiFi companion build (TCP port 5000), full-flash image. |
| `meck_twatch_s3_wifi.bin` | WiFi companion build, for a watch that already runs Meck. Config is kept. |
| `meck_twatch_s3_standalone-merged.bin` | Standalone build (no BLE/WiFi, 40 MHz cap -- maximum battery life), full-flash image. |
| `meck_twatch_s3_standalone.bin` | Standalone build -- flash this after BLE setup to keep your config. |

**T-Watch S3 Plus**

| File | Description |
|---|---|
| `meck_twatch_s3_plus_ble-merged.bin` | BLE companion build with maps, full-flash image -- recommended first flash for initial setup and backup via the MeshCore app. |
| `meck_twatch_s3_plus_ble.bin` | BLE companion build with maps, for a watch that already runs Meck. Config is kept. |
| `meck_twatch_s3_plus_wifi-merged.bin` | WiFi companion build with maps (TCP port 5000), full-flash image. |
| `meck_twatch_s3_plus_wifi.bin` | WiFi companion build with maps, for a watch that already runs Meck. Config is kept. |
| `meck_twatch_s3_plus_standalone-merged.bin` | Standalone build with maps, full-flash image. |
| `meck_twatch_s3_plus_standalone.bin` | Standalone build with maps, for a watch that already runs Meck. Config is kept. |

**T-Watch S3 Plus -- No Maps**

| File | Description |
|---|---|
| `meck_twatch_s3_plus_ble_nomaps-merged.bin` | BLE companion build, no maps, full-flash image. |
| `meck_twatch_s3_plus_ble_nomaps.bin` | BLE companion build, no maps, for a watch that already runs Meck. Config is kept. |
| `meck_twatch_s3_plus_standalone_nomaps-merged.bin` | Standalone build, no maps, 40 MHz cap, full-flash image. |
| `meck_twatch_s3_plus_standalone_nomaps.bin` | Standalone build, no maps, 40 MHz cap, for a watch that already runs Meck. Config is kept. |

All eight environments share their watch's partition table, so you can move between standalone, BLE, WiFi and (on the Plus) maps and no-maps with an ordinary flash at `0x10000` and keep your config either way.

---

## S3 Plus: No Maps Builds

Maps is the most expensive thing running on the S3 Plus. If you do not use it, the **No Maps** builds are the ones to run: they measured **21h24m** against 15-16 hours before this work, over a run that saw 6167 packets.

What changes:

- **The map screen is gone**, and the **Games** tile takes the Maps slot on the home grid -- the same layout the plain S3 has.
- **Tap to wake.** With the display off, a touch anywhere wakes the watch to the clock face. That gesture only wakes; it does not activate whatever is under your finger.
- **The side PWR key moves you around.** A single press on the clock face goes to the home UI, and a single press anywhere else returns you to the clock face. A press of four seconds or more is still a hardware power-off.
- **Tilt-to-wake is not active.** The wrist-raise wake is excluded on these builds, since the clock face is reached by a tap and moved by the PWR key.
- **GPS is unchanged.** The GNSS rail still boots off and is still available from Settings; only the map screen is removed.

Everything else -- messaging, contacts, DMs, discovery, repeater admin, trace, notes, steps, the settings screen -- is identical to the maps builds.

---

## Flashing Firmware

Download the latest firmware from the [Releases](https://github.com/pelgraine/Meck-Watch/releases) page. Each release includes two `.bin` types per variant:

| File Type | When to Use |
|-----------|-------------|
| `*-merged.bin` | **First-time flash** — bootloader, partition table, and firmware in one file. Flash at address `0x0`. |
| `*.bin` (non-merged) | **Upgrading existing firmware** — replaces the firmware and nothing else, so your config is kept. Flash at `0x10000`. |

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

Seven tiles are common to both watches — **Messages**, **Contacts**, **Settings**, **Discover**, **Trace**, **Notes**, and **Steps** — with the eighth slot differing by build:

- **S3 Plus** shows **Maps** (it has GPS).
- **S3** shows **Games** (Snake and Minesweeper) in that slot, since it has no GPS to plot.
- **S3 Plus, No Maps builds** show **Games** as well -- see [S3 Plus: No Maps Builds](#s3-plus-no-maps-builds).

Tap a tile to open its screen. To get back: **tap the top-right corner of the screen to return home from any screen** — this is the universal "go home" gesture across the whole watch UI. A **long press on the home screen** opens the channel picker (see [Channel Messages & the Channel Picker](#channel-messages--the-channel-picker)).

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

> On the S3 Plus **No Maps** builds the PWR key is also a navigation button: a single press on the clock face goes to the home UI, and a single press from anywhere else returns to the clock face. It remains the power control as well (hold four seconds or more to power off).

---

## Touch Gestures

Touch is the primary way to drive both watches, and the gesture set follows the same conventions across screens:

| Gesture | Typical Action |
|---------|----------------|
| Tap | Select / activate / open the item under your finger |
| Swipe up / down | Scroll or page lists (contacts, messages, settings, notes, discovery) |
| Swipe left / right | Cycle a filter or switch channel, depending on screen |
| Long press | Context action (e.g. open a channel from the picker, DM a contact, open the keyboard for text entry, edit a setting) |
| **Tap the top-right corner** | **Return to the home screen — works from every screen** |

Text entry — composing messages, notes, admin passwords, typed paths — is done through the on-screen **touch keyboard**, which opens when you tap into an editable field.

---

## Tilt-to-Wake & Lock Screen

With the display off, a **wrist-raise** (detected by the BMA423 accelerometer) wakes the watch straight to the clock/lock screen and restarts the idle timer — no button press needed.

On the S3 Plus **No Maps** builds this works differently: tilt-to-wake is not active, and the clock face is woken by **tapping the screen** instead. See [S3 Plus: No Maps Builds](#s3-plus-no-maps-builds).

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
| Swipe up / down | Page the list one screenful at a time (the view moves, your selection stays put), so every channel is reachable however many you have |
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

When you send a channel message, the watch tracks which repeaters re-flooded it. Selecting one of your own outgoing messages opens a **"Heard by"** overlay listing the distinct repeaters that echoed it, along with the signal quality (SNR) heard from each — a quick way to confirm your message is propagating and see which repeaters are carrying it. Each entry shows the repeater's hash and the SNR it heard you at, one per line: `(36e2) 10.7dB`.

On the **S3 Plus** this is reached via the boot button on a selected outgoing message; on the **S3**, via the PWR key.

The same overlay on an **incoming** message shows its route instead: the hop count and hash size, the time it was received (`Rx: 23 Jul 17:24`, in your local time), and the hex hop path. It also carries a **`[Reply]`** button in its bottom-right corner: tap it to open the composer pre-filled with `@[sender] `, the bracketed tag form other MeshCore devices and the companion app recognise. Reply is offered on received channel messages only -- not on your own sent messages, and not in the DM inbox. Any other tap dismisses the overlay as before. The received-time line also appears on Direct (0 hops) and Flood (no path) messages, and is left off your own sent messages, whose timestamp is a send time rather than a receive time. If the clock has never been set the line is omitted rather than showing a 1970 date.

---

## Notes

A lightweight notes screen stores `.txt` files on the internal LittleFS partition (there is no SD card). Tap while editing to open the touch keyboard; long-press to save and exit.

On the notes list, a **vertical swipe pages the list** one screenful at a time, so every saved note is reachable; a thin scrollbar appears on the right edge when the list overflows the view.

Notes are entered through the touch keyboard, so each note is capped at **133 characters** — the same length as a standard MeshCore message.

---

## Discovery & Last Heard

- **Discovery** runs an active scan for nearby nodes: the watch transmits a discover request, and repeaters and room servers answer it directly. The list holds **only those responses**, each with the SNR it was heard at -- adverts that happen to arrive while the scan is running are not listed. Swipe to scroll results, tap to add a node to contacts, long-press to rescan.
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

Inside repeater admin, **tapping the top of the screen goes back one level** rather than leaving, so your admin session and password survive: a response or error page returns to the command menu, the command menu returns to the category menu, parameter entry and confirmation return to the command menu, and a command still in flight is cancelled. The top-level category menu and the password prompt have nowhere further back, so a tap there returns you to the home UI. The footers tell you what a tap will do on each page -- `Tap top: Cancel`, `Tap top: No` with `Long Press: Yes`, or `Tap top: Back` with `Swipe:Scrll`.

The **path editor** lets you set a manual repeater chain for a contact, for direct routing. Open it from the Contacts screen using the button (short press on the S3, boot button on the Plus) with a contact selected.

Its **Mode** row (1 or 2 bytes per hop) follows the same convention as the settings list: **long press to enter edit, tap to cycle the value, long press again to leave**. While you are editing, the value is bracketed -- `Mode: <2B/hop>` -- so the edit state is visible.

Saving a path returns immediately. The contact record is written out a moment later in the background rather than inline, so the watch no longer freezes while it saves.

---

## Trace Route

The **Trace** tile runs a trace-route across the mesh — pick a contact or type a path, and the watch reports each hop back. Useful for diagnosing routing and confirming which repeaters a path traverses.

Its **Mode** row (1-byte or 2-byte path hashes) works like the path editor's: **long press to enter edit, tap to cycle, long press to leave**, with the value bracketed as `Mode: <2-byte>` while editing.

---

## Games

On the **S3**, and on the S3 Plus **No Maps** builds, the **Games** tile (in the slot the Plus otherwise uses for Maps) opens a small games menu with **Snake** and **Minesweeper**, both playable by touch.

**Snake** is steered entirely by tapping:

- **Tap anywhere** to start the game, and to restart after a game over.
- **While playing**, tap a zone relative to the centre of the board to steer: tap right of centre to go right, left to go left, above centre to go up, below to go down. The tap direction can't reverse the snake straight back on itself.
- **Tap the top-right corner** to exit back to the games menu, the same back gesture used across the watch UI.

---

## Maps (S3 Plus only)

The S3 Plus has an onboard MIA-M10Q GNSS receiver, so it carries the **Maps** tile — plotting contacts with known GPS positions on a scrollable map. The S3 has no GPS and shows the Games tile in that slot instead, as do the S3 Plus **No Maps** builds, which drop the map screen for battery life (see [S3 Plus: No Maps Builds](#s3-plus-no-maps-builds)).

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
- **Rx Log** — the packet sniffer (see [Rx Log](#rx-log))
- **WiFi** — network setup and a radio on/off toggle (WiFi builds only, see [WiFi Companion](#wifi-companion))
- **Experimental Features** submenu — the RX duty cycle battery saver (see [Experimental: RX Duty Cycle](#experimental-rx-duty-cycle))

### Navigating the settings list

The settings list follows one consistent model, shown in the on-screen footer (**Hold:Edit / Tap:Cycle**):

| Gesture | Action |
|---------|--------|
| Swipe up / down | Page the list one screenful at a time (the view scrolls; your selection stays put and its highlight reappears when the row scrolls back into view) |
| Tap a row | Select it |
| Long press | Activate the selected row — toggle it, open its submenu, or enter edit mode |

A tap never edits anything — activation and editing always require a **long press**, which prevents accidental edits while scrolling. Tap the **top-right corner** to return home.

### Editing values

Once a row is activated with a long press, how you edit depends on the type of value:

**Number rows — UTC offset, Path hash size, Spreading Factor (5–12), Coding Rate (5–8):**

| Gesture | Action |
|---------|--------|
| Tap the **upper half** of the screen | Increase the value |
| Tap the **lower half** of the screen | Decrease the value |
| Long press | Save and exit editing |

The footer shows **Tap:+/- Hold:Save** while editing.

**Radio Preset picker:**

| Gesture | Action |
|---------|--------|
| Tap the **upper half** | Next preset |
| Tap the **lower half** | Previous preset |
| Long press | Save and exit |

**Typed rows — Node name, Frequency, Bandwidth, Default Region, Add Channel, TX Power:** a long press opens the **touch keyboard**, pre-filled with the current value. Type the new value and press OK to commit. Frequency accepts exact values like `916.575`; Bandwidth snaps to the nearest valid value (31.25, 62.5, 125, 250, or 500 kHz); TX power is clamped to 1–30 dBm. Clearing the **Default Region** text and pressing OK reverts the device to unscoped (see [Region Scope](#region-scope)).

### Radio Presets

The Radio Preset picker carries ready-made profiles for common regional configurations — selecting one sets frequency, bandwidth, SF, CR, and TX power together, and you can then fine-tune any individual parameter afterwards:

| Preset | Freq (MHz) | BW (kHz) | SF | CR | TX (dBm) |
|--------|-----------|----------|----|----|----------|
| Australia | 915.800 | 250 | 10 | 5 | 22 |
| Australia (Mid) | 915.075 | 125 | 9 | 5 | 22 |
| Australia (Narrow) | 916.575 | 62.5 | 7 | 8 | 22 |
| Australia: SA, WA | 923.125 | 62.5 | 8 | 8 | 22 |
| Australia: QLD | 923.125 | 62.5 | 8 | 5 | 22 |
| EU/UK (Narrow) | 869.618 | 62.5 | 8 | 8 | 14 |
| EU/UK (Long Range) | 869.525 | 250 | 11 | 5 | 14 |
| EU/UK (Medium Range) | 869.525 | 250 | 10 | 5 | 14 |
| Czech Republic (Narrow) | 869.432 | 62.5 | 7 | 5 | 14 |
| EU 433 (Long Range) | 433.650 | 250 | 11 | 5 | 14 |
| New Zealand | 917.375 | 250 | 11 | 5 | 22 |
| New Zealand (Narrow) | 917.375 | 62.5 | 7 | 5 | 22 |
| Portugal 433 | 433.375 | 62.5 | 9 | 6 | 14 |
| Portugal 868 | 869.618 | 62.5 | 7 | 6 | 14 |
| Switzerland | 869.618 | 62.5 | 8 | 8 | 14 |
| USA/Canada (Recommended) | 910.525 | 62.5 | 7 | 5 | 22 |
| Vietnam | 920.250 | 250 | 11 | 5 | 22 |

> Some options present on the larger Meck devices are intentionally not shown on the watch — dark mode, font-style selection, and the large-font toggle are omitted, and the SD-dependent config export/import and WiFi options do not apply.

---

## Experimental: RX Duty Cycle

> **Known issue in v1.0 -- leave this off.** On the S3 the feature currently does the opposite of what it is meant to. With Nap Depth at Light an S3 standalone runs flat in roughly 6 hours; the same watch on the same build with the duty cycle off measured **14h27m**. The drain is far larger than the radio itself can account for and is still under investigation. It remains off by default.

**Settings → Experimental Features** holds an opt-in battery saver: instead of listening continuously, the SX1262 duty-cycles its receive window using the chip's own hardware listen/sleep mechanism. It is **off by default**, and nothing changes until you turn it on.

| Row | What it does |
|-----|--------------|
| **Rx Duty Cycle** | On/Off toggle. Persists across reboots. |
| **Nap Depth** | How deeply the radio naps between listen windows: **Light** (~31% asleep), **Medium** (~50%), or **Aggressive** (~64%). Long-press to cycle. Remembered independently of the toggle, so your chosen depth is restored when you switch the feature back on. |

Toggling it takes effect straight away -- the radio changes mode as soon as you flip the switch, rather than waiting for the next packet.

While the duty cycle is on, the Radio page of the home screen replaces its **Noise floor** row with **Last Pkt RSSI**. The noise-floor sampler cannot run when the radio is asleep between listen windows, so that row would otherwise sit at zero; the last received packet's RSSI is a real measurement taken with the receiver awake.

Both rows only appear at **SF9 and below** — at higher spreading factors the longer symbols gain nothing worth the wake cost, and a **"Rx Duty: SF9 or lower only"** note is shown instead. If a preset's symbols are too short to carve out a safe sleep window, the radio automatically falls back to continuous RX, so the mechanism itself never drops a packet.

**The trade-off:** most mesh networks run a 16-symbol preamble at SF9, and against short preambles the fraction of single transmissions you miss roughly tracks the fraction of time the radio is asleep. In practice flooded channel traffic arrives via multiple repeater copies and direct messages are retried, so the everyday impact is mostly added latency rather than lost messages — but it is a genuine reception-versus-battery trade, which is why it's an opt-in dial rather than a default. Start at **Light** and judge the behaviour on your own network.

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

- **Device-wide default:** Set in **Settings → Default Region** (default scope) — a long press opens the touch keyboard. Applies to all channels and DMs unless a channel overrides it. To remove a region, clear the text and press OK: the device reverts to unscoped.
- **Per-channel:** In the Channels submenu, a channel can carry its own scope, overriding the device default for that channel only. Set it from the channel configuration screen, from the Channels submenu, or over serial with `set channel.scope <index> <region>` (the index is the number shown by `get channels`, with no angle brackets).

Both are stored as the derived transport key at the moment you set them, so sending never re-derives anything. Per-channel scopes persist across reboots from v1.0 onward -- earlier firmware saved the channel name and secret only, so a per-channel scope silently reverted to the device default on every restart.

---

## Channels: Public, Private & Sharing

Channels are managed from **Settings → Channels**. You can add a public channel (a plain `#name`) or a **private channel** with a randomly generated secret, and edit or delete existing ones. `MAX_GROUP_CHANNELS` is 20 on the watch builds, plus the DMs slot.

Private channels and channel sharing follow the standard Meck/MeshCore model — a private channel is encrypted with its secret, and sharing sends the channel to a contact so they can join it.

---

## Bluetooth (BLE)

On the **BLE** builds, BLE is disabled by default at boot, consistent with the other Meck devices and standalone-first operation. Enable it to connect the MeshCore companion app or the [Meck-Mycelium web app](https://pelgraine.github.io/Meck-Mycelium). **Standalone** builds omit BLE entirely and run fully self-contained — no phone required for any function.

---

## WiFi Companion

The **WiFi** builds (`meck_twatch_s3_wifi`, `meck_twatch_s3_plus_wifi`) expose the MeshCore companion interface over TCP on **port 5000**, so the [web app](https://app.meshcore.nz), meshcore.js, or the Python CLI can connect over your local network — the same interface the T-Deck WiFi variants provide. The full touch UI is unchanged; the network connection is in addition to it, not instead of it.

**Setting up WiFi on the watch.** Settings carries two WiFi rows on these builds:

| Row | What it does |
|-----|--------------|
| **WiFi** | Shows the connected SSID, or `(not connected)`. Activating it scans for networks and walks scan → pick an SSID (up to 10 listed) → type the password on the touch keyboard → connect. |
| **WiFi Radio** | On/off toggle for the radio itself. |

Credentials are saved to `/wifi.cfg` on the internal LittleFS partition, so the watch reconnects on its own after a reboot and the settings survive a firmware update. The `WIFI_SSID` / `WIFI_PWD` build flags remain available as compile-time defaults if you would rather bake credentials into the image.

Once connected, the WiFi home page shows the SSID, the watch's IP address, the port, and whether a client is attached. Point your client at `<watch IP>:5000`.

> **Battery:** the WiFi radio is expensive on a watch-sized cell, and these builds also stay at 80 MHz. Use a WiFi build for setup, backups and desk work; wear a standalone build.

---

## Battery & Charging

Both watches ship with **LiHV** (high-voltage lithium) cells: 3.8 V nominal, 4.35 V maximum charge. The S3's cell is a 470 mAh Tewaycell 552530; the S3 Plus uses a Tewaycell 112530-2P of the same chemistry. Meck charges both to 4.35 V to use the full capacity of the LiHV cell.

> LilyGo's published battery specifications for these watches (1500 mAh, 3.7 V) do not match the cells actually fitted — the figures above are taken from the physical cell labels. If you replace a cell, confirm its chemistry: charging a standard 3.7 V / 4.2 V-max cell to 4.35 V is unsafe.

Charge over USB-C. The AXP2101 PMU manages charging; a moderate charge current is recommended for cell longevity.

### Runtime

Two changes made a large difference to how long the watches last:

- **The display-off throttle.** While the display is off -- which is nearly all the time -- the firmware yields the CPU so the idle task can drop into a low-power wait. This was present but never actually running until v0.9. On the BLE builds it is skipped while Bluetooth is enabled, to keep the connection responsive.
- **The 40 MHz cap** on `meck_twatch_s3_standalone` and `meck_twatch_s3_plus_standalone_nomaps`, which holds both the idle and boost clocks down instead of only the idle clock.

Measured results:

| Build | Configuration | Runtime |
|---|---|---|
| S3 standalone | throttle + 40 MHz cap, duty cycle off | 14h27m (was 9h30m) |
| S3 Plus standalone, no maps | throttle + 40 MHz cap, GPS off, duty cycle off | 21h24m (was 15-16h) |

The S3 Plus figure came from a run that saw 6167 packets, so it is not an idle-bench number. The maps builds have not been re-measured since the throttle fix. Both figures were measured with the [RX duty cycle](#experimental-rx-duty-cycle) **off** -- see the known issue in that section before turning it on.

> **Reading the battery percentage.** The gauge is markedly non-linear on these LiHV cells. Over the 21h24m run it fell about 0.7%/hour for the first three hours, then 5-12.6%/hour through the middle, then sat almost still between 26% and 19% for nearly three hours before dropping fast. Do not draw conclusions from short percentage deltas, particularly in the 19-26% band.

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
- Build for your target using the environment names in [Build Environment Options](#build-environment-options).

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