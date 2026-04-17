#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>  

#define F(x) x
#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 2

extern uint32_t g_ms;

inline uint32_t millis() { return g_ms += 10; }
inline void delay(uint32_t ms) { g_ms += ms; }

inline void pinMode(uint8_t, uint8_t) {}
int digitalRead(uint8_t pin);
void arduino_stub_set_pin(uint8_t pin, int value);
void arduino_stub_reset_pins();

struct FakeSerial {
  void print(const char* s) { printf("%s", s); }
  void print(int v, int = 10) { printf("%d", v); }
  void print(unsigned int v, int = 10) { printf("%u", v); }
  void print(long v, int = 10) { printf("%ld", v); }
  void print(unsigned long v, int = 10) { printf("%lu", v); }

  void println(const char* s) { printf("%s\n", s); }
  void println(int v, int = 10) { printf("%d\n", v); }
  void println(unsigned int v, int = 10) { printf("%u\n", v); }
  void println(long v, int = 10) { printf("%ld\n", v); }
  void println(unsigned long v, int = 10) { printf("%lu\n", v); }
  int available(void) { return 0; }
  int read(void) { return -1; }
};

#define HEX 16

extern FakeSerial Serial;
extern FakeSerial Serial1;

inline void NVIC_SystemReset() { exit(1); }