#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <time.h>
#include "MyMesh.h"

#if defined(MECK_TWATCH)
  // Dual-firmware slot switching (boot escape hatch, Boot Other Slot button,
  // post-switch image validation) -- same calls the companion firmware uses.
  #include <esp_ota_ops.h>
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[160];

// For power saving
unsigned long lastActive = 0; // mark last active time
unsigned long nextSleepinSecs = 120; // next sleep in seconds. The first sleep (if enabled) is after 2 minutes from boot

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

#if defined(MECK_TWATCH)
  // Boot escape hatch -- same behaviour as the companion firmware: a FRESH
  // PWR key press within the first 800 ms after board.begin() switches boot
  // to the other OTA slot and restarts, so the companion slot stays reachable
  // even if this build dies later in setup. The power-on hold itself cannot
  // trigger it: latched edges are cleared first, so only a
  // release-and-re-press counts. Reads the same AXP2101 IRQ registers
  // PMUButton polls.
  {
    auto* hatch_pmu = board.getPMU();
    if (hatch_pmu) {
      hatch_pmu->getIrqStatus();
      hatch_pmu->clearIrqStatus();       // discard the power-on press
      bool hatch_held = false;
      unsigned long hatch_t0 = millis();
      while (millis() - hatch_t0 < 800) {
        delay(30);
        hatch_pmu->getIrqStatus();
        if (hatch_pmu->isPekeyNegativeIrq()) hatch_held = true;   // fresh press
        if (hatch_pmu->isPekeyPositiveIrq()) hatch_held = false;  // released
        hatch_pmu->clearIrqStatus();
      }
      if (hatch_held) {
        const esp_partition_t* hatch_next = esp_ota_get_next_update_partition(NULL);
        if (hatch_next && esp_ota_set_boot_partition(hatch_next) == ESP_OK) {
          Serial.printf("Hatch: PWR key held, rebooting into %s\n", hatch_next->label);
          esp_restart();
        }
      }
    }
  }
#endif

  // For power saving
  lastActive = millis(); // mark last active time since boot

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM* fs;
#if defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

#if defined(MECK_TWATCH)
  // OTA boot validation -- same call the companion firmware makes once it has
  // started up. If this image arrived by OTA upload it boots as
  // PENDING_VERIFY; without this call (when CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE
  // is set) the bootloader rolls back to the previous slot on the next reboot.
  {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
      if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
          Serial.println("OTA: New firmware validated, rollback cancelled");
        } else {
          Serial.println("OTA: WARNING - failed to cancel rollback");
        }
      }
    }
  }
#endif

  // send out initial Advertisement to the mesh
  the_mesh.sendSelfAdvertisement(16000);
}

void loop() {
  int len = strlen(command);
  // `command` must stay NUL-terminated within its bounds. If it ever isn't,
  // strlen() above can return >= sizeof(command) and the loop below would then
  // index past the buffer, so clamp defensively.
  if (len >= (int)sizeof(command)) {
    command[0] = 0;
    len = 0;
  }
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
    }
    if (c == '\r') break;
  }
  if (len == sizeof(command)-1) {  // buffer full: treat as a completed line
    command[sizeof(command)-2] = '\r';  // place end-of-line marker inside the buffer
    command[sizeof(command)-1] = 0;     // keep the buffer NUL-terminated
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    Serial.print('\n');
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

#if defined(MECK_TWATCH) && defined(DISPLAY_CLASS)
  // Boot Other Slot: the button on the repeater screen sets a request flag
  // (after a confirming second tap); flip the boot slot here and restart into
  // the firmware in the other slot. No extra save is done first: the repeater
  // CLI saves each setting at the time it is changed.
  if (ui_task.isBootOtherSlotRequested()) {
    ui_task.clearBootOtherSlotRequest();
    const esp_partition_t* slotNext = esp_ota_get_next_update_partition(NULL);
    if (slotNext && esp_ota_set_boot_partition(slotNext) == ESP_OK) {
      Serial.printf("Slot switch: rebooting into %s\n", slotNext->label);
      ui_task.showMessage("Rebooting -", "Please Wait...");
      delay(400);
      ESP.restart();
    } else {
      Serial.println("Slot switch: esp_ota_set_boot_partition failed");
      ui_task.showMessage("Slot switch failed", "");
      delay(1500);
    }
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

  if (the_mesh.getNodePrefs()->powersaving_enabled &&
      the_mesh.millisHasNowPassed(lastActive + nextSleepinSecs * 1000)) {
    if (!the_mesh.hasPendingWork()) {
      board.sleep(1800);
      lastActive = millis();
      nextSleepinSecs = 5;
    } else {
      nextSleepinSecs += 5;
    }
  }
}
