#include <Arduino.h>

#include "logger.h"
#include "storage_manager.h"

static LogEntry _log[EVM_LOG_MAX_ENTRIES];
static uint32_t _count = 0;
static uint32_t _persist_dropped = 0;
static bool _flushing = false;

void logger_init(void) {
  _count = 0;
  _persist_dropped = 0;
  _flushing = false;
}

void logger_log(LogEventType type, uint32_t ts, uint32_t data) {
  if (_count >= EVM_LOG_MAX_ENTRIES) {
    if (_flushing) {
      _persist_dropped++;
      return;
    }
    logger_flush();

    if (_count >= EVM_LOG_MAX_ENTRIES) {
      _persist_dropped++;
      return;
    }
  }

  _log[_count].type = type;
  _log[_count].timestamp_ms = ts;
  _log[_count].data = data;
  _count++;
}

void logger_flush(void) {
  if (_flushing) return;
  _flushing = true;

  uint32_t n = _count;
  for (uint32_t i = 0; i < n; i++) {
    logger_print_entry(i);
    if (!storage_manager_write_log(&_log[i])) {
      _persist_dropped++;
    }
  }

  _count = 0;
  _flushing = false;
}

void logger_print_entry(uint32_t i) {
  if (i >= _count) return;
  Serial.print(i);
  Serial.print(F(" T="));
  Serial.print(_log[i].timestamp_ms);
  Serial.print(F(" TYPE="));
  Serial.print(_log[i].type);
  Serial.print(F(" DATA="));
  Serial.println(_log[i].data);
}

uint32_t logger_get_count(void) { return _count; }

bool logger_get_entry(uint32_t i, LogEntry* out) {
  if (i >= _count || !out) return false;
  *out = _log[i];
  return true;
}

uint32_t logger_get_persist_dropped(void) { return _persist_dropped; }