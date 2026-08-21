#pragma once

#include "CustomSX1280.h"
#include "RadioLibWrappers.h"

class CustomSX1280Wrapper : public RadioLibWrapper {
public:
  CustomSX1280Wrapper(CustomSX1280& radio, mesh::MainBoard& board) : RadioLibWrapper(radio, board) { }
  bool isReceivingPacket() override {
    return ((CustomSX1280 *)_radio)->isReceiving();
  }
  float getCurrentRSSI() override {
    return ((CustomSX1280 *)_radio)->getRSSI(false);   // instantaneous RSSI
  }
  float getLastRSSI() const override { return ((CustomSX1280 *)_radio)->getRSSI(); }
  float getLastSNR() const override { return ((CustomSX1280 *)_radio)->getSNR(); }

  // packetScore(): the base-class version is used (assumes SF10, this build's
  // default). Unlike SX126x, SX128x keeps spreadingFactor protected, so it
  // cannot be read here.

  virtual void powerOff() override {
    // standby() only -- deliberately no sleep(). LilyGo's own LilyGoWatchS3.cpp
    // skips radio.sleep() on the SX1280 revision ("SX1280 died here, the reason
    // is not analyzed yet"). At watch shutdown the AXP2101 cuts the rail moments
    // later anyway.
    ((CustomSX1280 *)_radio)->standby();
  }
};
