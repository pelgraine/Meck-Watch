#include <Arduino.h>
#include "variant.h"
#include "target.h"

TWatchS3Board board;

// LoRa radio on its own SPI bus (the display uses SPI3_HOST separately).
// SX1262 (HPD16B3 module) on the sub-GHz revision; SX1280 on the 2.4 GHz
// revision (MECK_TWATCH_1280) -- same pins, only the radio silicon differs.
static SPIClass loraSpi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, loraSpi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

// No GNSS and no environment sensors on this board, so the base SensorManager
// is used. node_lat/node_lon stay at 0 and advert location policy has nothing
// to publish.
SensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  PMUButton user_btn(board);
#endif

bool radio_init() {
  // NOTE: board.begin() is called by main.cpp setup() before radio_init();
  // Wire is already initialised there with the correct pins.
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  loraSpi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  return radio.std_init(&loraSpi);
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);

#if defined(MECK_TWATCH_1280)
  // Preamble stays at the fixed LORA_PREAMBLE set by std_init(). The SF-based
  // preamble switch and the stuck-IRQ deadline plumbing below are
  // SX126x-specific and do not exist on CustomSX1280.
#else
  // Longer preamble for low SF improves reliability -- each symbol is shorter
  // at low SF, so more symbols are needed for reliable detection.
  uint16_t preamble = (sf <= 9) ? 32 : 16;
  radio.setPreambleLength(preamble);

  // Stuck-IRQ deadlines for CustomSX1262::isReceiving() (upstream PR #2977):
  // derived from the params just applied, including our actual preamble length.
  PacketMillis pm = radio_driver.calcMaxPacketMillis(sf, bw, cr, (uint8_t)preamble);
  radio.setPreambleMillis(pm.preambleMillis);
  radio.setMaxPayloadMillis(pm.payloadMillis);
#endif
}

void radio_set_tx_power(uint8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}

void radio_reset_agc() {
#if defined(MECK_TWATCH_1280)
  // No-op on the SX1280: there is no SX126x-style boosted-gain register to
  // re-assert (setRxBoostedGainMode does not exist on this chip), and putting
  // the radio into standby here would leave RX dead until the next transmit.
  // RadioLibWrapper::resetAGC() still performs its generic startReceive cycle.
#else
  // Power-saving RX gain on the watch (deliberate): boosted gain costs
  // ~0.6-0.7 mA continuously for ~+2 dB sensitivity. The SetRxGain write
  // itself is what resets the AGC; false selects the power-saving value.
  radio.setRxBoostedGainMode(false);
#endif
}