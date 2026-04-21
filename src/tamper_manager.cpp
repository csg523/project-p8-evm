#include "tamper_manager.h"
#include "event_manager.h"
#include "storage_manager.h"
#include "logger.h"
#include <Arduino.h>
#include <string.h>
#include <stddef.h>

static TamperManagerState _tm;
//tm has which tamper happened,how many times,timestamps
static volatile bool _locked = false;
//if locked is true then it means tamper happend,system is locked.
static volatile uint8_t _latched_flags = 0;
//latched flags-which tamper was detected.

//active -what signal tamper is triggered
static const struct {
    uint8_t pin;                                  //which hardware pin is used for this tamper
    uint8_t active;
    TamperType type;
    uint8_t interrupt_mode;
} SENSORS[3] = {
    { TAMPER_PIN_CASE,    LOW,  TAMPER_CASE_OPEN, FALLING },
    { TAMPER_PIN_VOLTAGE, HIGH, TAMPER_VOLTAGE,   RISING  },
    { TAMPER_PIN_CLOCK,   HIGH, TAMPER_CLOCK,     RISING  }
};

//ISR fxn,run immediately when tamper is detected, set the latched flag for that tamper type, if system is already locked then do nothing
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
//turns off all tamper events interrupts to avoid multiple triggers after first one is detected
static void disable_tamper_interrupts(void) {
    detachInterrupt(digitalPinToInterrupt(TAMPER_PIN_CASE));
    detachInterrupt(digitalPinToInterrupt(TAMPER_PIN_VOLTAGE));
    detachInterrupt(digitalPinToInterrupt(TAMPER_PIN_CLOCK));
}

void tamper_manager_init(void) {
    memset(&_tm, 0, sizeof(_tm));//clear all previous tamper data
    _locked = false;
    _latched_flags = 0;

    pinMode(TAMPER_PIN_CASE, INPUT_PULLUP);//input_pullup means default value high for the pin
    pinMode(TAMPER_PIN_VOLTAGE, INPUT_PULLUP);
    pinMode(TAMPER_PIN_CLOCK, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(TAMPER_PIN_CASE), tamper_isr_case, FALLING);//when this pin goes from high to low run this isr code
    attachInterrupt(digitalPinToInterrupt(TAMPER_PIN_VOLTAGE), tamper_isr_voltage, RISING);
    attachInterrupt(digitalPinToInterrupt(TAMPER_PIN_CLOCK), tamper_isr_clock, RISING);
}

void tamper_manager_poll(void) {
    if (_locked) return;

    uint8_t flags;

    noInterrupts();//interrupts are turned off to prevent data corruption while reading share data
    flags = _latched_flags;//latched flags shared bw isr
    _latched_flags = 0;
    interrupts();

    if (flags == 0) return;

    tamper_manager_report((TamperType)flags, millis());
}

void tamper_manager_report(TamperType type, uint32_t timestamp_ms) {
    if (_locked) return;

    const uint32_t allowed =                        //creates a bitmask of all valid tamper types by oring them together, thus allowed will have bit 0,1,2 set if those tampers are valid
        (uint32_t)TAMPER_CASE_OPEN |
        (uint32_t)TAMPER_VOLTAGE |
        (uint32_t)TAMPER_CLOCK;

    uint32_t flags = (uint32_t)type;
    if (flags == 0 || (flags & ~allowed) != 0) {
        logger_log(LOG_ERROR, timestamp_ms, 0xE002); // invalid tamper flags
        return;
    }

    disable_tamper_interrupts();
//active_flags is what tampers have happened so far
    _tm.active_flags |= flags;//add new tamper value without removing old one 
    _tm.tamper_count++;
    if (_tm.first_tamper_ms == 0) _tm.first_tamper_ms = timestamp_ms;
    _tm.last_tamper_ms = timestamp_ms;

    TamperRecord rec;//record of tamper event to be stored in storage manager, it has type of tamper,timestamp and crc for data integrity
    memset(&rec, 0, sizeof(rec));//clear the space for the size of record
    rec.type = (TamperType)flags;//what kind of tamper happend
    rec.timestamp_ms = timestamp_ms;
    rec.crc = evm_crc16((const uint8_t*)&rec, offsetof(TamperRecord, crc));//calculate crc on all data except crc itself and store it in crc field of record
//storage append tamper will try to store the record in storage and return the result of operation, if it is not successful then log the error with logger
    EvmResult sr = storage_append_tamper(&rec);
    if (sr != EVM_OK) {
        logger_log(LOG_ERROR, timestamp_ms, (uint32_t)sr);
    }

    logger_log(LOG_TAMPER, timestamp_ms, flags); // single forensic tamper log
    _locked = true;
}

bool tamper_manager_is_triggered(void) { return _tm.active_flags != 0; }//has any tamper happened or not
bool tamper_manager_is_locked(void) { return _locked; }
uint32_t tamper_manager_get_flags(void) { return _tm.active_flags; }
uint32_t tamper_manager_get_count(void) { return _tm.tamper_count; }
//get full tamper manager state for debugging and inspection
const TamperManagerState* tamper_manager_get_state(void) { return &_tm; }
