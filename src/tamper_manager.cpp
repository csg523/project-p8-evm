#include "tamper_manager.h"
#include "event_manager.h"
#include "storage_manager.h"
#include "logger.h"
#include <Arduino.h>
#include <string.h>
#include <stddef.h>

static TamperManagerState _tm;
static bool _locked = false;

static uint32_t _trigger_ms[3] = {0, 0, 0};
static bool _triggered[3] = {false, false, false};

static const struct {
    uint8_t pin;
    uint8_t active;
    TamperType type;
} SENSORS[3] = {
    { TAMPER_PIN_CASE,    LOW,  TAMPER_CASE_OPEN },
    { TAMPER_PIN_VOLTAGE, HIGH, TAMPER_VOLTAGE   },
    { TAMPER_PIN_CLOCK,   HIGH, TAMPER_CLOCK     }
};

void tamper_manager_init(void) {
    memset(&_tm, 0, sizeof(_tm));
    _locked = false;
    for (int i = 0; i < 3; i++) {
        _trigger_ms[i] = 0;
        _triggered[i] = false;
    }

    pinMode(TAMPER_PIN_CASE, INPUT_PULLUP);
    pinMode(TAMPER_PIN_VOLTAGE, INPUT_PULLUP);
    pinMode(TAMPER_PIN_CLOCK, INPUT_PULLUP);
}

void tamper_manager_poll(void) {
    if (_locked) return;

    uint32_t now = millis();
    for (uint8_t i = 0; i < 3; i++) {
        bool active = (digitalRead(SENSORS[i].pin) == SENSORS[i].active);

        if (active && !_triggered[i]) {
            _trigger_ms[i] = now;
            _triggered[i] = true;
            continue;
        }

        if (!active) {
            _triggered[i] = false;
            continue;
        }

        if (_triggered[i] && (now - _trigger_ms[i]) >= EVM_DEBOUNCE_MS) {
            ParsedEvent evt = EVT_EMPTY;
            evt.type = EVT_TAMPER;
            evt.timestamp_ms = now;
            evt.data.tamper.tamper_flags = (uint32_t)SENSORS[i].type;
            event_manager_enqueue(&evt);
            _triggered[i] = false;
        }
    }
}

void tamper_manager_report(TamperType type, uint32_t timestamp_ms) {
    // This is called by supervisor when EVT_TAMPER is dispatched.
    _tm.active_flags |= (uint32_t)type;
    _tm.tamper_count++;
    if (_tm.first_tamper_ms == 0) _tm.first_tamper_ms = timestamp_ms;
    _tm.last_tamper_ms = timestamp_ms;

    TamperRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.type = type;
    rec.timestamp_ms = timestamp_ms;
    rec.crc = evm_crc16((const uint8_t*)&rec, offsetof(TamperRecord, crc));
    storage_append_tamper(&rec);

    logger_log(LOG_TAMPER, timestamp_ms, (uint32_t)type);
    _locked = true;
}

bool tamper_manager_is_triggered(void) { return _tm.active_flags != 0; }
bool tamper_manager_is_locked(void) { return _locked; }
uint32_t tamper_manager_get_flags(void) { return _tm.active_flags; }
uint32_t tamper_manager_get_count(void) { return _tm.tamper_count; }
const TamperManagerState* tamper_manager_get_state(void) { return &_tm; }
