#pragma once
#include <Arduino.h>
#include <functional>

class Led;

// GPIO0 button: tap toggles the relay, long-hold factory-resets (SPEC.md).
//   release < 1s        -> onTap (toggle relay)
//   hold 1s..5s         -> arming, blue accelerates from slow to fast
//   hold reaches 5s     -> committed: solid, then off 2s, 5 flashes, onReset
//   release 1s..5s      -> abort, no tap, no reset
// The toggle fires on release (not press) so a hold never also toggles.
class Button {
public:
  using Callback = std::function<void()>;

  void begin(Led* led);
  void update(uint32_t now);

  void onTap(Callback cb) { onTap_ = cb; }
  void onFactoryReset(Callback cb) { onReset_ = cb; }

  // Tunable timings (ms).
  static const uint32_t kTapMax    = 1000;
  static const uint32_t kArmFull   = 5000;
  static const uint32_t kSolidHold = 400;
  static const uint32_t kConfirmOff = 2000;

private:
  enum class State : uint8_t { Idle, Pressing, Arming, Committed, Confirm };

  bool readPressed() const;  // active low with pullup

  Led* led_ = nullptr;
  Callback onTap_;
  Callback onReset_;
  State state_ = State::Idle;
  uint32_t pressStart_ = 0;
  uint32_t phaseStart_ = 0;
  uint32_t blinkToggle_ = 0;
  bool blinkOn_ = false;
};
