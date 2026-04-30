#include "Arduino.h"

uint32_t g_ms = 0;
FakeSerial Serial;
FakeSerial Serial1;

static int g_pin_state[256];

void arduino_stub_reset_pins() {
  for (int i = 0; i < 256; ++i) g_pin_state[i] = HIGH;
}

void arduino_stub_set_pin(uint8_t pin, int value) { g_pin_state[pin] = value; }

int digitalRead(uint8_t pin) { return g_pin_state[pin]; }