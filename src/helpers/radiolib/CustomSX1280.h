#pragma once

#include <RadioLib.h>

// SX1280 LoRa IRQ flags used to detect an in-progress reception
#define SX128X_IRQ_HEADER_VALID                RADIOLIB_SX128X_IRQ_HEADER_VALID
#define SX128X_IRQ_PREAMBLE_DETECTED           RADIOLIB_SX128X_IRQ_PREAMBLE_DETECTED
#define SX128X_IRQ_SYNC_WORD_VALID             RADIOLIB_SX128X_IRQ_SYNC_WORD_VALID

// 2.4 GHz LoRa (SX1280). Borrowed from GrayHatGuy/MeshCore branch
// feat/lilygo-twatch-s3-and-2.4ghz-lora, trimmed to the ESP32-only SPI path
// this tree uses (see CustomSX1262.h for the same trim).
class CustomSX1280 : public SX1280 {
  public:
    CustomSX1280(Module *mod) : SX1280(mod) { }

    bool std_init(SPIClass* spi = NULL) {
  #ifdef LORA_CR
      uint8_t cr = LORA_CR;
  #else
      uint8_t cr = 5;
  #endif

  #ifdef LORA_PREAMBLE
      uint16_t preamble = LORA_PREAMBLE;
  #else
      uint16_t preamble = 16;
  #endif

  #if defined(P_LORA_SCLK)
      if (spi) spi->begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI);
  #endif

      // RADIOLIB_SX128X_SYNC_WORD_PRIVATE == 0x12 (the MeshCore "private" network word)
      int status = begin(LORA_FREQ, LORA_BW, LORA_SF, cr, RADIOLIB_SX128X_SYNC_WORD_PRIVATE, LORA_TX_POWER, preamble);
      if (status != RADIOLIB_ERR_NONE) {
        Serial.print("ERROR: radio init failed: ");
        Serial.println(status);
        return false;  // fail
      }

      setCRC(2);

  #if defined(SX128X_RXEN) || defined(SX128X_TXEN)
    #ifndef SX128X_RXEN
      #define SX128X_RXEN RADIOLIB_NC
    #endif
    #ifndef SX128X_TXEN
      #define SX128X_TXEN RADIOLIB_NC
    #endif
      // External RF switch (e.g. LilyGo T3-S3 2.4G). The T-Watch S3 SX1280
      // has no external RX/TX enable pins, so these are left undefined there
      // and this block compiles out.
      setRfSwitchPins(SX128X_RXEN, SX128X_TXEN);
  #endif

      return true;  // success
    }

    bool isReceiving() {
      uint16_t irq = getIrqStatus();
      bool detected = (irq & SX128X_IRQ_HEADER_VALID) || (irq & SX128X_IRQ_PREAMBLE_DETECTED)
                       || (irq & SX128X_IRQ_SYNC_WORD_VALID);
      return detected;
    }
};
