#include "Relay.h"
#include "Pins.h"

void Relay::begin(bool initialOn) {
  pinMode(PIN_RELAY, OUTPUT);
  on_ = initialOn;
  digitalWrite(PIN_RELAY, on_ ? RELAY_ON : RELAY_OFF);
}

void Relay::set(bool on) {
  if (on == on_) return;
  on_ = on;
  digitalWrite(PIN_RELAY, on_ ? RELAY_ON : RELAY_OFF);
  if (onChange_) onChange_(on_);
}
