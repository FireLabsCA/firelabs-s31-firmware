#include "Button.h"
#include "Led.h"
#include "Pins.h"

void Button::begin(Led* led) {
  led_ = led;
  pinMode(PIN_BUTTON, INPUT_PULLUP);
}

bool Button::readPressed() const {
  return digitalRead(PIN_BUTTON) == LOW;
}

void Button::update(uint32_t now) {
  const bool pressed = readPressed();

  switch (state_) {
    case State::Idle:
      if (pressed) { state_ = State::Pressing; pressStart_ = now; }
      break;

    case State::Pressing:
      if (!pressed) {
        if (now - pressStart_ < kTapMax && onTap_) onTap_();  // tap
        state_ = State::Idle;
      } else if (now - pressStart_ >= kTapMax) {
        state_ = State::Arming;
        blinkToggle_ = now;
        blinkOn_ = false;
      }
      break;

    case State::Arming: {
      if (!pressed) {                 // released before 5s: abort
        led_->clearOverride();
        state_ = State::Idle;
        break;
      }
      const uint32_t held = now - pressStart_;
      if (held >= kArmFull) {         // committed
        state_ = State::Committed;
        phaseStart_ = now;
        led_->override(255);          // solid
        break;
      }
      // Accelerate blink as the hold approaches 5s: period 600ms -> 80ms.
      const float p = (float)(held - kTapMax) / (float)(kArmFull - kTapMax);
      const uint32_t period = (uint32_t)(600 - 520 * p);
      if (now - blinkToggle_ >= period) { blinkOn_ = !blinkOn_; blinkToggle_ = now; }
      led_->override(blinkOn_ ? 255 : 0);
      break;
    }

    case State::Committed:  // solid for a beat, then the confirm sequence
      if (now - phaseStart_ >= kSolidHold) {
        state_ = State::Confirm;
        phaseStart_ = now;
        led_->override(0);
      }
      break;

    case State::Confirm: {
      const uint32_t t = now - phaseStart_;
      if (t < kConfirmOff) { led_->override(0); break; }  // 2s dark
      const uint32_t ft = t - kConfirmOff;                // 5 flashes, 100/100
      const uint8_t idx = ft / 200;
      if (idx >= 5) {
        led_->clearOverride();
        state_ = State::Idle;
        if (onReset_) onReset_();
        break;
      }
      led_->override((ft % 200) < 100 ? 255 : 0);
      break;
    }
  }
}
