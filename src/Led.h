#pragma once
#include <Arduino.h>

// Blue LED on a single priority channel (SPEC.md "LED behavior").
// Highest active condition wins. The reset animation overrides everything via
// override()/clearOverride(), driven by Button.
class Led {
public:
  enum class Status : uint8_t {
    Off,         // normal, working
    SetupAp,     // captive-portal setup, slow blink, full
    Connecting,  // joining wifi, fast blink, full
    NoLoad,      // relay on but ~0W, solid, dimmed
  };

  void begin();
  void update(uint32_t now);

  void setStatus(Status s) { status_ = s; }
  void setIndicatorPercent(uint8_t pct) { indicatorPct_ = pct; }

  // Identify: three fast full-brightness blinks, a pause, repeat. Sits above the
  // status patterns but below the button reset animation.
  void setIdentify(bool on) { identify_ = on; idInit_ = false; }
  bool identifying() const { return identify_; }

  // Reset animation override. brightness 0..255; inversion handled internally.
  void override(uint8_t brightness) { overrideActive_ = true; overrideVal_ = brightness; }
  void clearOverride() { overrideActive_ = false; }

private:
  void write(uint8_t brightness);  // 0 = off, 255 = full

  Status status_ = Status::Off;
  uint8_t indicatorPct_ = 30;
  bool overrideActive_ = false;
  uint8_t overrideVal_ = 0;
  uint32_t lastToggle_ = 0;
  bool blinkOn_ = false;
  bool identify_ = false;
  bool idInit_ = false;
  uint32_t idStart_ = 0;
};
