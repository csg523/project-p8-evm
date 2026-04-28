#include "storage_manager.h"
#include <Arduino.h>
#include <stddef.h>
#include <string.h>
#include "logger.h"
#include <FlashStorage_SAMD.h>

// ─────────────────────────────────────────────
//  Storage Manager – append-only NVM
//
//  Votes/tampers: RAM simulation for now.
//  Logs: persisted to flash via FlashStorage_SAMD.
// ─────────────────────────────────────────────

#define MAX_STORED_VOTES EVM_MAX_VOTES

// Tamper record storage: sized to match the number of distinct tamper types
// (3 sensors, each can trigger once before lockdown) plus a small margin.
#define MAX_STORED_TAMPERS 8
#define MAX_STORED_LOGS EVM_LOG_MAX_ENTRIES

static VoteRecord _nvm_votes[MAX_STORED_VOTES];
static bool _nvm_valid[MAX_STORED_VOTES];
static uint32_t _write_ptr = 0;
static uint32_t _record_count = 0;
static uint32_t _last_vote_id = 0;
static bool _full = false;
static bool _nvm_initialised = false;

static TamperRecord _nvm_tampers[MAX_STORED_TAMPERS];
static uint32_t _tamper_count = 0;

static uint32_t _log_count = 0;

#define LOG_FLASH_MAGIC 0x45564D31u  // "EVM1"

typedef struct {
  uint32_t magic;
  uint32_t count;
  LogEntry logs[MAX_STORED_LOGS];
} FlashLogStore;

FlashStorage(evm_logs_store, FlashLogStore);

static FlashLogStore _flash_logs_cache;

static void flash_logs_init() {
  evm_logs_store.read(_flash_logs_cache);
  if (_flash_logs_cache.magic != LOG_FLASH_MAGIC ||
      _flash_logs_cache.count > MAX_STORED_LOGS) {
    memset(&_flash_logs_cache, 0, sizeof(_flash_logs_cache));
    _flash_logs_cache.magic = LOG_FLASH_MAGIC;
    _flash_logs_cache.count = 0;
    evm_logs_store.write(_flash_logs_cache);
  }
  _log_count = _flash_logs_cache.count;
}

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

void storage_reset(void) {
  memset(_nvm_votes, 0, sizeof(_nvm_votes));
  memset(_nvm_valid, 0, sizeof(_nvm_valid));
  memset(_nvm_tampers, 0, sizeof(_nvm_tampers));

  _nvm_initialised = false;
  _write_ptr = 0;
  _record_count = 0;
  _last_vote_id = 0;
  _tamper_count = 0;
  _log_count = 0;
  _full = false;

  memset(&_flash_logs_cache, 0, sizeof(_flash_logs_cache));
  _flash_logs_cache.magic = LOG_FLASH_MAGIC;
  _flash_logs_cache.count = 0;
  evm_logs_store.write(_flash_logs_cache);
}

void storage_init(void) {
  if (!_nvm_initialised) {
    memset(_nvm_votes, 0, sizeof(_nvm_votes));
    memset(_nvm_valid, 0, sizeof(_nvm_valid));
    memset(_nvm_tampers, 0, sizeof(_nvm_tampers));
    _nvm_initialised = true;
  }

  _write_ptr = 0;
  _record_count = 0;
  _last_vote_id = 0;
  _tamper_count = 0;
  _log_count = 0;
  _full = false;

  flash_logs_init();

  // Scan all slots and validate CRC
  for (uint32_t i = 0; i < MAX_STORED_VOTES; i++) {
    VoteRecord* r = &_nvm_votes[i];

    // Blank slot (vote_id == 0 in RAM sim = unwritten)
    if (r->vote_id == 0 && r->candidate_id == 0 && r->timestamp_ms == 0) {
      _write_ptr = i;
      break;
    }

    uint16_t expected = evm_crc16((const uint8_t*)r, offsetof(VoteRecord, crc));
    if (r->crc != expected) {
      // Corrupted / partial write – stop recovery here
      _nvm_valid[i] = false;
      _write_ptr = i;
      logger_log(LOG_ERROR, millis(), i);
      break;
    }

    _nvm_valid[i] = true;
    _record_count++;
    if (r->vote_id > _last_vote_id) _last_vote_id = r->vote_id;
    _write_ptr = i + 1;
  }

  if (_write_ptr >= MAX_STORED_VOTES) _full = true;
}

EvmResult storage_append_vote(const VoteRecord* rec) {
  if (!rec) return EVM_ERR_INVALID;
  if (_full) return EVM_ERR_FULL;

  uint16_t check = evm_crc16((const uint8_t*)rec, offsetof(VoteRecord, crc));
  if (check != rec->crc) return EVM_ERR_CRC;

  _nvm_votes[_write_ptr] = *rec;
  _nvm_valid[_write_ptr] = true;
  _write_ptr++;
  _record_count++;
  if (rec->vote_id > _last_vote_id) _last_vote_id = rec->vote_id;
  if (_write_ptr >= MAX_STORED_VOTES) _full = true;
  return EVM_OK;
}

bool storage_manager_write_log(const LogEntry* entry) {
  if (!entry) return false;
  if (_flash_logs_cache.count >= MAX_STORED_LOGS) return false;
  _flash_logs_cache.logs[_flash_logs_cache.count++] = *entry;
  _log_count = _flash_logs_cache.count;
  evm_logs_store.write(_flash_logs_cache);
  return true;
}

uint32_t storage_get_log_count(void) {
  return _log_count;
}

uint32_t storage_get_log_capacity(void) {
  return MAX_STORED_LOGS;
}

EvmResult storage_append_tamper(const TamperRecord* rec) {
  if (!rec) return EVM_ERR_INVALID;

  if (_tamper_count >= MAX_STORED_TAMPERS) {
    // Tamper store full — log the overflow but do not overwrite existing
    // evidence (first-tamper evidence is most forensically valuable).
    logger_log(LOG_ERROR, rec->timestamp_ms, 0xE001);
    return EVM_ERR_FULL;
  }

  uint16_t check = evm_crc16((const uint8_t*)rec, offsetof(TamperRecord, crc));
  if (check != rec->crc) return EVM_ERR_CRC;

  _nvm_tampers[_tamper_count++] = *rec;
  return EVM_OK;
}

EvmResult storage_read_record(uint32_t index, VoteRecord* out) {
  if (!out || index >= _record_count) return EVM_ERR_INVALID;
  if (!_nvm_valid[index]) return EVM_ERR_CRC;
  *out = _nvm_votes[index];
  return EVM_OK;
}

uint32_t storage_get_last_vote_id(void) { return _last_vote_id; }
uint32_t storage_get_record_count(void) { return _record_count; }
bool storage_is_full(void) { return _full; }

uint32_t storage_recover(uint32_t* out_last_vote_id) {
  storage_init();
  if (out_last_vote_id) *out_last_vote_id = storage_get_last_vote_id();
  return storage_get_record_count();
}

void storage_dump_serial() {
  Serial.println(F("=== STORAGE DUMP ==="));
  uint32_t count = storage_get_record_count();
  for (uint32_t i = 0; i < count; ++i) {
    VoteRecord rec;
    if (storage_read_record(i, &rec) != EVM_OK) {
      Serial.print(F("Index "));
      Serial.print(i);
      Serial.println(F(": CRC ERROR"));
      continue;
    }
    Serial.print(F("Index "));
    Serial.print(i);
    Serial.print(F(" vote_id="));
    Serial.print(rec.vote_id);
    Serial.print(F(" candidate_id="));
    Serial.print(rec.candidate_id);
    Serial.print(F(" ts="));
    Serial.print(rec.timestamp_ms);
    Serial.print(F(" crc=0x"));
    Serial.println(rec.crc, HEX);
  }
}
