#pragma once
#include <Arduino.h>

// Diagnostic logging over UART0 at 115200, enabled with -DFL_DIAG. In a normal
// build DBG() compiles to nothing and UART0 stays with the CSE7766.
#ifdef FL_DIAG
  #define DBG(fmt, ...) do { Serial.printf("[FL] " fmt "\r\n", ##__VA_ARGS__); } while (0)
#else
  #define DBG(fmt, ...) do {} while (0)
#endif
