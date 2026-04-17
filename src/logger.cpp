#include "logger.h"
#include "storage_manager.h"
#include <Arduino.h>

static LogEntry _log[EVM_LOG_MAX_ENTRIES];
static uint32_t _count = 0;

void logger_init(void) { _count = 0; }

void logger_log(LogEventType type, uint32_t ts, uint32_t data) {
    if (_count >= EVM_LOG_MAX_ENTRIES) {
        // Buffer full — flush to NVM to make room
        logger_flush();
    }
    _log[_count++] = { type, ts, data };
}

void logger_flush(void) {
    for (uint32_t i = 0; i < _count; i++) {
        logger_print_entry(i);
        storage_manager_write_log(&_log[i]);
    }
    _count = 0;
}

void logger_print_entry(uint32_t i) {
    if (i >= _count) return;
    Serial.print(i); Serial.print(F(" T=")); Serial.print(_log[i].timestamp_ms);
    Serial.print(F(" TYPE=")); Serial.print(_log[i].type);
    Serial.print(F(" DATA=")); Serial.println(_log[i].data);
}

uint32_t logger_get_count(void) { return _count; }
bool logger_get_entry(uint32_t i, LogEntry* out) {
    if (i >= _count || !out) return false;
    *out = _log[i];
    return true;
}