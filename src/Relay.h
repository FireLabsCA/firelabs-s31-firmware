#pragma once
#include <Arduino.h>
#include <functional>

// GPIO12 relay. The red LED is wired to this line, so it tracks the relay
// state in hardware with no extra code.
class Relay {
public:
  using ChangeCb = std::function<void(bool)>;

  void begin(bool initialOn);
  void set(bool on);
  void toggle() { set(!on_); }
  bool isOn() const { return on_; }

  void onChange(ChangeCb cb) { onChange_ = cb; }

private:
  bool on_ = false;
  ChangeCb onChange_;
};
