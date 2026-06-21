#pragma once
#include <Arduino.h>

// Sonoff S31 pin map (ESP-12 variant). See SPEC.md.
// GPIO0  button, active low, also the boot-strap pin (low at power-on = flash mode)
// GPIO12 relay; the red LED is wired to this line in hardware, not controllable
// GPIO13 blue LED, PWM, active low, the only software LED
// GPIO3  CSE7766 data in, 4800 8E1, receive only

static const uint8_t PIN_BUTTON = 0;
static const uint8_t PIN_RELAY  = 12;
static const uint8_t PIN_LED     = 13;  // blue, active low
static const uint8_t PIN_CSE_RX  = 3;   // UART0 RX

// Relay is active high (HIGH = closed = on, also lights the red LED).
static const uint8_t RELAY_ON  = HIGH;
static const uint8_t RELAY_OFF = LOW;
