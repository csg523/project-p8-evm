#include <Arduino.h>
#include <stddef.h>
#include <string.h>

#include "tamper_manager.h"
#include "event_manager.h"
#include "logger.h"
#include "storage_manager.h"

static TamperManagerState _tm;
static volatile bool _locked = false;
static volatile uint8_t _latched_flags = 0;

static const struct {
  uint8_t pin;
  uint8_t active;
  TamperType type;
  uint8_t interrupt_mode;
} SENSORS[3] = {{TAMPER_PIN_CASE, LOW, TAMPER_CASE_OPEN, FALLING},
                {TAMPER_PIN_VOLTAGE, HIGH, TAMPER_VOLTAGE, RISING},
                {TAMPER_PIN_CLOCK, HIGH, TAMPER_CLOCK, RISING}};

static void tamper_isr_case(void) {
  if (_locked) return;
  _latched_flags |= (uint8_t)TAMPER_CASE_OPEN;
}

static void tamper_isr_voltage(void) {
  if (_locked) return;
  _latched_flags |= (uint8_t)TAMPER_VOLTAGE;
}

static void tamper_isr_clock(void) {
  if (_locked) return;
  _latched_flags |= (uint8_t)TAMPER_CLOCK;
}

static void disable_tamper_interrupts(void) {
  detachInterrupt(digitalPinToInterrupt(TAMPER_PIN_CASE));
  detachInterrupt(digitalPinToInterrupt(TAMPER_PIN_VOLTAGE));
  detachInterrupt(digitalPinToInterrupt(TAMPER_PIN_CLOCK));
}

void tamper_manager_init(void) {
  memset(&_tm, 0, sizeof(_tm));
  _locked = false;
  _latched_flags = 0;

  pinMode(TAMPER_PIN_CASE, INPUT_PULLUP);
  pinMode(TAMPER_PIN_VOLTAGE, INPUT_PULLUP);
  pinMode(TAMPER_PIN_CLOCK, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(TAMPER_PIN_CASE), tamper_isr_case,
                  FALLING);
  attachInterrupt(digitalPinToInterrupt(TAMPER_PIN_VOLTAGE), tamper_isr_voltage,
                  RISING);
  attachInterrupt(digitalPinToInterrupt(TAMPER_PIN_CLOCK), tamper_isr_clock,
                  RISING);
}

void tamper_manager_poll(void) {
  if (_locked) return;

  uint8_t flags;

  noInterrupts();
  flags = _latched_flags;
  _latched_flags = 0;
  interrupts();

  if (flags == 0) return;

  tamper_manager_report((TamperType)flags, millis());
}

void tamper_manager_report(TamperType type, uint32_t timestamp_ms) {
  if (_locked) return;

  const uint32_t allowed = (uint32_t)TAMPER_CASE_OPEN |
                           (uint32_t)TAMPER_VOLTAGE | (uint32_t)TAMPER_CLOCK;

  uint32_t flags = (uint32_t)type;
  if (flags == 0 || (flags & ~allowed) != 0) {
    logger_log(LOG_ERROR, timestamp_ms, 0xE002);
    return;
  }

  disable_tamper_interrupts();
  _tm.active_flags |= flags;
  _tm.tamper_count++;
  if (_tm.first_tamper_ms == 0) _tm.first_tamper_ms = timestamp_ms;
  _tm.last_tamper_ms = timestamp_ms;

  TamperRecord rec;
  memset(&rec, 0, sizeof(rec));
  rec.type = (TamperType)flags;
  rec.timestamp_ms = timestamp_ms;
  rec.crc = evm_crc16((const uint8_t*)&rec, offsetof(TamperRecord, crc));

  EvmResult sr = storage_append_tamper(&rec);
  if (sr != EVM_OK) {
    logger_log(LOG_ERROR, timestamp_ms, (uint32_t)sr);
  }

  logger_log(LOG_TAMPER, timestamp_ms, flags);
  _locked = true;
}

void tamper_manager_lock(void) {
  disable_tamper_interrupts();
  _locked = true;
}

bool tamper_manager_is_triggered(void) { return _tm.active_flags != 0; }

bool tamper_manager_is_locked(void) { return _locked; }

uint32_t tamper_manager_get_flags(void) { return _tm.active_flags; }

uint32_t tamper_manager_get_count(void) { return _tm.tamper_count; }

const TamperManagerState* tamper_manager_get_state(void) { return &_tm; }
