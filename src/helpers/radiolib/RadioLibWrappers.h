#pragma once

#include <Mesh.h>
#include <RadioLib.h>

class RadioLibWrapper : public mesh::Radio {
protected:
  PhysicalLayer* _radio;
  mesh::MainBoard* _board;
  uint32_t n_recv, n_sent;
  int16_t _noise_floor, _threshold;
  uint16_t _num_floor_samples;
  int32_t _floor_sample_sum;
  bool _dutyEnabled;   // MCU-driven RX duty cycle runtime toggle (watch)
  uint8_t _dutyLevel;  // nap depth: 0=Light, 1=Medium, 2=Aggressive

  void idle();
  void startRecv();
#if defined(MECK_RX_DUTY_CYCLE)
  int  armDutyCycleRx();   // arm hardware RX duty cycle for the current SF/BW + nap level
#endif
  float packetScoreInt(float snr, int sf, int packet_len);
  virtual bool isReceivingPacket() =0;

public:
  RadioLibWrapper(PhysicalLayer& radio, mesh::MainBoard& board) : _radio(&radio), _board(&board) { n_recv = n_sent = 0; _dutyEnabled = false; _dutyLevel = 0; }

  void begin() override;
  virtual void powerOff() { _radio->standby(); _radio->sleep(); }
  int recvRaw(uint8_t* bytes, int sz) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  bool isInRecvMode() const override;
  bool isChannelActive();

  bool isReceiving() override { 
    if (isReceivingPacket()) return true;

    return isChannelActive();
  }

  virtual float getCurrentRSSI() =0;

  int getNoiseFloor() const override { return _noise_floor; }
  void triggerNoiseFloorCalibrate(int threshold) override;
  void resetAGC() override;

  // MCU-driven RX duty cycle (watch): runtime toggle, persisted in NodePrefs.
  void setDutyCycleEnabled(bool en) { _dutyEnabled = en; }
  bool isDutyCycleActive() const override { return _dutyEnabled; }
  // Nap depth: 0=Light (~31% asleep), 1=Medium (~50%), 2=Aggressive (~64%).
  void setDutyLevel(uint8_t lvl) { _dutyLevel = (lvl > 2) ? 2 : lvl; }

  void loop() override;

  uint32_t getPacketsRecv() const { return n_recv; }
  uint32_t getPacketsSent() const { return n_sent; }
  void resetStats() { n_recv = n_sent = 0; }

  virtual float getLastRSSI() const override;
  virtual float getLastSNR() const override;

  float packetScore(float snr, int packet_len) override { return packetScoreInt(snr, 10, packet_len); }  // assume sf=10
};

/**
 * \brief  an RNG impl using the noise from the LoRa radio as entropy.
 *         NOTE: this is VERY SLOW!  Use only for things like creating new LocalIdentity
*/
class RadioNoiseListener : public mesh::RNG {
  PhysicalLayer* _radio;
public:
  RadioNoiseListener(PhysicalLayer& radio): _radio(&radio) { }

  void random(uint8_t* dest, size_t sz) override {
    for (int i = 0; i < sz; i++) {
      dest[i] = _radio->randomByte() ^ (::random(0, 256) & 0xFF);
    }
  }
};