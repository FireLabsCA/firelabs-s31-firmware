#include "Led.h"
#include "Pins.h"

static const uint16_t kRange = 255;

void Led::begin() {
  pinMode(PIN_LED, OUTPUT);
  analogWriteRange(kRange);
  analogWriteFreq(1000);
  write(0);
}

void Led::write(uint8_t brightness) {
  // Active low: full brightness drives the pin low.
  analogWrite(PIN_LED, kRange - brightness);
}

void Led::update(uint32_t now) {
  if (overrideActive_) {
    write(overrideVal_);
    return;
  }

  if (identify_) {  // three fast blinks, pause, repeat, full brightness
    if (!idInit_) { idStart_ = now; idInit_ = true; }
    const uint32_t on = 120, off = 120, pause = 760;
    const uint32_t group = 3 * (on + off);
    uint32_t c = (now - idStart_) % (group + pause);
    bool lit = (c < group) && ((c % (on + off)) < on);
    write(lit ? 255 : 0);
    return;
  }

  switch (status_) {
    case Status::SetupAp:  // slow blink, 1s on / 1s off
      if (now - lastToggle_ >= 1000) { blinkOn_ = !blinkOn_; lastToggle_ = now; }
      write(blinkOn_ ? 255 : 0);
      break;

    case Status::Connecting:  // fast blink, ~3Hz
      if (now - lastToggle_ >= 160) { blinkOn_ = !blinkOn_; lastToggle_ = now; }
      write(blinkOn_ ? 255 : 0);
      break;

    case Status::NoLoad:  // solid, dimmed
      write((uint16_t)255 * indicatorPct_ / 100);
      break;

    case Status::Off:
    default:
      write(0);
      break;
  }
}
