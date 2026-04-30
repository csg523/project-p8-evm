#include "storage_manager.h"
#include <Arduino.h>
#include <stddef.h>
#include <string.h>
#include "logger.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Storage Manager – append-only NVM
//
//  Flash layout (FlashStorage_SAMD emulated EEPROM):
//    evm_votes_store  – up to 200 VoteRecord entries + magic + count
//    evm_tampers_store– up to 8  TamperRecord entries + magic + count
//    evm_logs_store   – up to 64 LogEntry entries     + magic + count
//    evm_state_store  – election state snapshot for power-loss detection
//
//  The state snapshot is written on every state transition. On boot,
//  if the snapshot says VOTING_ACTIVE or VOTING_CLOSED and the reset
//  cause was RESET_POWER_LOSS, the supervisor treats this as a tamper.
// ─────────────────────────────────────────────────────────────────────────────

#ifndef NATIVE_TEST
#include <FlashStorage_SAMD.h>
#endif

#define MAX_STORED_VOTES    EVM_MAX_VOTES
#define MAX_STORED_TAMPERS  8
#define MAX_STORED_LOGS     EVM_LOG_MAX_ENTRIES

#define LOG_FLASH_MAGIC     0x45564D31u  // "EVM1"
#define VOTE_FLASH_MAGIC    0x45564D56u  // "EVMV"
#define TAMPER_FLASH_MAGIC  0x45564D54u  // "EVMT"
#define STATE_FLASH_MAGIC   0x45564D53u  // "EVMS"

// ─── Flash store structs ──────────────────────────────────────────────────────

typedef struct {
    uint32_t    magic;
    uint32_t    count;
    LogEntry    logs[MAX_STORED_LOGS];
} FlashLogStore;

typedef struct {
    uint32_t    magic;
    uint32_t    count;
    VoteRecord  votes[MAX_STORED_VOTES];
} FlashVoteStore;

typedef struct {
    uint32_t      magic;
    uint32_t      count;
    TamperRecord  tampers[MAX_STORED_TAMPERS];
} FlashTamperStore;

// Election state snapshot – written on every transition so the SAMD21 can
// detect power-loss mid-election on the next boot.
typedef struct {
    uint32_t      magic;
    ElectionState state;       // last known state before power loss
    uint32_t      vote_count;  // votes accepted so far (for display in report)
    uint32_t      saved_ms;    // millis() when last saved
} FlashStateStore;

// ─── Flash objects ────────────────────────────────────────────────────────────

#ifdef NATIVE_TEST
// ── Stub: emulate FlashStorage with plain RAM structs ────────────────────────
static FlashLogStore    _flash_logs_cache;
static FlashVoteStore   _flash_votes_cache;
static FlashTamperStore _flash_tampers_cache;
static FlashStateStore  _flash_state_cache;

static void flash_logs_write(void)    { /* RAM only in native test */ }
static void flash_votes_write(void)   {}
static void flash_tampers_write(void) {}
static void flash_state_write(void)   {}

static void flash_logs_read(void)    {}
static void flash_votes_read(void)   {}
static void flash_tampers_read(void) {}
static void flash_state_read(void)   {}

#else
// ── Real FlashStorage_SAMD objects ───────────────────────────────────────────
FlashStorage(evm_logs_store,    FlashLogStore);
FlashStorage(evm_votes_store,   FlashVoteStore);
FlashStorage(evm_tampers_store, FlashTamperStore);
FlashStorage(evm_state_store,   FlashStateStore);

static FlashLogStore    _flash_logs_cache;
static FlashVoteStore   _flash_votes_cache;
static FlashTamperStore _flash_tampers_cache;
static FlashStateStore  _flash_state_cache;

static void flash_logs_write(void)    { evm_logs_store.write(_flash_logs_cache); }
static void flash_votes_write(void)   { evm_votes_store.write(_flash_votes_cache); }
static void flash_tampers_write(void) { evm_tampers_store.write(_flash_tampers_cache); }
static void flash_state_write(void)   { evm_state_store.write(_flash_state_cache); }

static void flash_logs_read(void)    { evm_logs_store.read(_flash_logs_cache); }
static void flash_votes_read(void)   { evm_votes_store.read(_flash_votes_cache); }
static void flash_tampers_read(void) { evm_tampers_store.read(_flash_tampers_cache); }
static void flash_state_read(void)   { evm_state_store.read(_flash_state_cache); }
#endif

// ─── RAM mirrors ─────────────────────────────────────────────────────────────

static VoteRecord  _nvm_votes[MAX_STORED_VOTES];
static bool        _nvm_valid[MAX_STORED_VOTES];
static uint32_t    _write_ptr    = 0;
static uint32_t    _record_count = 0;
static uint32_t    _last_vote_id = 0;
static bool        _full         = false;
static bool        _nvm_initialised = false;

static TamperRecord _nvm_tampers[MAX_STORED_TAMPERS];
static uint32_t     _tamper_count = 0;

static uint32_t _log_count = 0;

// ─── CRC-16 (CCITT) ──────────────────────────────────────────────────────────

extern "C" uint16_t evm_crc16(const uint8_t* data, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// ─── Private init helpers ─────────────────────────────────────────────────────

static void flash_logs_init(void) {
    flash_logs_read();
    if (_flash_logs_cache.magic != LOG_FLASH_MAGIC ||
        _flash_logs_cache.count > MAX_STORED_LOGS) {
        memset(&_flash_logs_cache, 0, sizeof(_flash_logs_cache));
        _flash_logs_cache.magic = LOG_FLASH_MAGIC;
        _flash_logs_cache.count = 0;
        flash_logs_write();
    }
    _log_count = _flash_logs_cache.count;
}

static void flash_votes_init(void) {
    flash_votes_read();
    if (_flash_votes_cache.magic != VOTE_FLASH_MAGIC ||
        _flash_votes_cache.count > MAX_STORED_VOTES) {
        memset(&_flash_votes_cache, 0, sizeof(_flash_votes_cache));
        _flash_votes_cache.magic = VOTE_FLASH_MAGIC;
        _flash_votes_cache.count = 0;
        flash_votes_write();
    }
}

static void flash_tampers_init(void) {
    flash_tampers_read();
    if (_flash_tampers_cache.magic != TAMPER_FLASH_MAGIC ||
        _flash_tampers_cache.count > MAX_STORED_TAMPERS) {
        memset(&_flash_tampers_cache, 0, sizeof(_flash_tampers_cache));
        _flash_tampers_cache.magic = TAMPER_FLASH_MAGIC;
        _flash_tampers_cache.count = 0;
        flash_tampers_write();
    }
    // Restore RAM mirror from flash
    _tamper_count = _flash_tampers_cache.count;
    for (uint32_t i = 0; i < _tamper_count; i++) {
        _nvm_tampers[i] = _flash_tampers_cache.tampers[i];
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void storage_reset(void) {
    memset(_nvm_votes,   0, sizeof(_nvm_votes));
    memset(_nvm_valid,   0, sizeof(_nvm_valid));
    memset(_nvm_tampers, 0, sizeof(_nvm_tampers));

    _nvm_initialised = false;
    _write_ptr    = 0;
    _record_count = 0;
    _last_vote_id = 0;
    _tamper_count = 0;
    _log_count    = 0;
    _full         = false;

    memset(&_flash_logs_cache,    0, sizeof(_flash_logs_cache));
    memset(&_flash_votes_cache,   0, sizeof(_flash_votes_cache));
    memset(&_flash_tampers_cache, 0, sizeof(_flash_tampers_cache));
    memset(&_flash_state_cache,   0, sizeof(_flash_state_cache));

    _flash_logs_cache.magic    = LOG_FLASH_MAGIC;
    _flash_votes_cache.magic   = VOTE_FLASH_MAGIC;
    _flash_tampers_cache.magic = TAMPER_FLASH_MAGIC;
    // Intentionally leave state magic at 0 so no snapshot appears valid.

    flash_logs_write();
    flash_votes_write();
    flash_tampers_write();
    flash_state_write();
}

void storage_init(void) {
    if (!_nvm_initialised) {
        memset(_nvm_votes,   0, sizeof(_nvm_votes));
        memset(_nvm_valid,   0, sizeof(_nvm_valid));
        memset(_nvm_tampers, 0, sizeof(_nvm_tampers));
        _nvm_initialised = true;
    }

    _write_ptr    = 0;
    _record_count = 0;
    _last_vote_id = 0;
    _tamper_count = 0;
    _log_count    = 0;
    _full         = false;

    flash_votes_init();
    flash_tampers_init();
    flash_logs_init();

    // Recover vote records from flash with CRC validation
    uint32_t count = _flash_votes_cache.count;
    for (uint32_t i = 0; i < count; i++) {
        VoteRecord* r = &_flash_votes_cache.votes[i];
        uint16_t expected = evm_crc16((const uint8_t*)r, offsetof(VoteRecord, crc));
        if (r->crc != expected) {
            // Corrupted record – truncate recovery here
            logger_log(LOG_ERROR, millis(), (uint32_t)(0xC000u | i));
            break;
        }
        _nvm_votes[i] = *r;
        _nvm_valid[i] = true;
        _record_count++;
        _write_ptr++;
        if (r->vote_id > _last_vote_id) _last_vote_id = r->vote_id;
    }

    if (_write_ptr >= MAX_STORED_VOTES) _full = true;
}

// ─── Vote records ─────────────────────────────────────────────────────────────

EvmResult storage_append_vote(const VoteRecord* rec) {
    if (!rec) return EVM_ERR_INVALID;
    if (_full) return EVM_ERR_FULL;

    uint16_t check = evm_crc16((const uint8_t*)rec, offsetof(VoteRecord, crc));
    if (check != rec->crc) return EVM_ERR_CRC;

    _nvm_votes[_write_ptr] = *rec;
    _nvm_valid[_write_ptr] = true;

    _flash_votes_cache.votes[_write_ptr] = *rec;
    _flash_votes_cache.count = _write_ptr + 1;
    flash_votes_write();

    _write_ptr++;
    _record_count++;
    if (rec->vote_id > _last_vote_id) _last_vote_id = rec->vote_id;
    if (_write_ptr >= MAX_STORED_VOTES) _full = true;
    return EVM_OK;
}

EvmResult storage_read_record(uint32_t index, VoteRecord* out) {
    if (!out || index >= _record_count) return EVM_ERR_INVALID;
    *out = _flash_votes_cache.votes[index];
    return EVM_OK;
}

uint32_t storage_get_last_vote_id(void) { return _last_vote_id; }
uint32_t storage_get_record_count(void) { return _record_count; }
bool     storage_is_full(void)          { return _full; }

uint32_t storage_recover(uint32_t* out_last_vote_id) {
    storage_init();
    if (out_last_vote_id) *out_last_vote_id = storage_get_last_vote_id();
    return storage_get_record_count();
}

void storage_dump_serial(void) {
    Serial.println(F("=== VOTE STORAGE DUMP ==="));
    for (uint32_t i = 0; i < _record_count; ++i) {
        VoteRecord rec;
        if (storage_read_record(i, &rec) != EVM_OK) {
            Serial.print(F("Index ")); Serial.print(i);
            Serial.println(F(": CRC ERROR"));
            continue;
        }
        Serial.print(F("VOTE idx=")); Serial.print(i);
        Serial.print(F(" id="));      Serial.print(rec.vote_id);
        Serial.print(F(" cand="));    Serial.print(rec.candidate_id);
        Serial.print(F(" ts="));      Serial.print(rec.timestamp_ms);
        Serial.print(F(" crc=0x"));   Serial.println(rec.crc, HEX);
    }
}

// ─── Tamper records ───────────────────────────────────────────────────────────

EvmResult storage_append_tamper(const TamperRecord* rec) {
    if (!rec) return EVM_ERR_INVALID;

    if (_tamper_count >= MAX_STORED_TAMPERS) {
        // First-tamper evidence is most forensically valuable; do not overwrite.
        logger_log(LOG_ERROR, rec->timestamp_ms, 0xE001);
        return EVM_ERR_FULL;
    }

    uint16_t check = evm_crc16((const uint8_t*)rec, offsetof(TamperRecord, crc));
    if (check != rec->crc) return EVM_ERR_CRC;

    _nvm_tampers[_tamper_count] = *rec;

    // Persist to flash immediately – tamper evidence must survive power loss.
    _flash_tampers_cache.tampers[_tamper_count] = *rec;
    _flash_tampers_cache.count = _tamper_count + 1;
    flash_tampers_write();

    _tamper_count++;
    return EVM_OK;
}

uint32_t storage_get_tamper_count(void) { return _tamper_count; }

EvmResult storage_read_tamper(uint32_t index, TamperRecord* out) {
    if (!out || index >= _tamper_count) return EVM_ERR_INVALID;
    *out = _nvm_tampers[index];
    return EVM_OK;
}

void storage_dump_tampers_serial(void) {
    Serial.println(F("=== TAMPER DUMP ==="));
    if (_tamper_count == 0) {
        Serial.println(F("  (no tamper records)"));
        return;
    }
    for (uint32_t i = 0; i < _tamper_count; i++) {
        Serial.print(F("TAMPER idx="));   Serial.print(i);
        Serial.print(F(" flags=0x"));     Serial.print(_nvm_tampers[i].type, HEX);
        Serial.print(F(" ts="));          Serial.print(_nvm_tampers[i].timestamp_ms);
        Serial.print(F(" crc=0x"));       Serial.println(_nvm_tampers[i].crc, HEX);
    }
}

// ─── Election state snapshot ──────────────────────────────────────────────────
//
// This is the key mechanism for power-loss tamper detection.
//
// Every time the supervisor transitions state, it calls
// storage_save_election_state(new_state, vote_count).
//
// On the next boot, storage_load_election_snapshot() returns the last
// saved state.  If that state was VOTING_ACTIVE and the reset cause was
// RESET_POWER_LOSS, the supervisor treats the restart as a tamper event.
//
// storage_clear_election_snapshot() is called only when the election
// transitions to VOTING_CLOSED or TAMPER_DETECTED normally, so a future
// cold-boot does not misfire.

void storage_save_election_state(ElectionState s, uint32_t vote_count) {
    _flash_state_cache.magic      = STATE_FLASH_MAGIC;
    _flash_state_cache.state      = s;
    _flash_state_cache.vote_count = vote_count;
    _flash_state_cache.saved_ms   = millis();
    flash_state_write();
}

bool storage_load_election_snapshot(ElectionState* out_state,
                                    uint32_t*      out_vote_count) {
    flash_state_read();
    if (_flash_state_cache.magic != STATE_FLASH_MAGIC) return false;
    if (out_state)      *out_state      = _flash_state_cache.state;
    if (out_vote_count) *out_vote_count = _flash_state_cache.vote_count;
    return true;
}

void storage_clear_election_snapshot(void) {
    memset(&_flash_state_cache, 0, sizeof(_flash_state_cache));
    // magic = 0 means "no valid snapshot"
    flash_state_write();
}

// ─── Log records ──────────────────────────────────────────────────────────────

bool storage_manager_write_log(const LogEntry* entry) {
    if (!entry) return false;
    if (_flash_logs_cache.count >= MAX_STORED_LOGS) return false;
    _flash_logs_cache.logs[_flash_logs_cache.count++] = *entry;
    _log_count = _flash_logs_cache.count;
    flash_logs_write();
    return true;
}

uint32_t storage_get_log_count(void)    { return _log_count; }
uint32_t storage_get_log_capacity(void) { return MAX_STORED_LOGS; }
