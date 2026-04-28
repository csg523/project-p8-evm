#include "vote_manager.h"

#include <Arduino.h>
#include <stddef.h>
#include <string.h>

#include "logger.h"
#include "storage_manager.h"

static VoteManagerState _vm;
static uint32_t _tally[EVM_MAX_CANDIDATES];

void vote_manager_init(void) {
  _vm.candidate_count = EVM_CANDIDATE_COUNT;
  _vm.votes_processed = 0;
  _vm.duplicates_rejected = 0;
  _vm.last_vote_id = storage_get_last_vote_id();
  memset(_tally, 0, sizeof(_tally));

  uint32_t n = storage_get_record_count();
  for (uint32_t i = 0; i < n; i++) {
    VoteRecord r;
    if (storage_read_record(i, &r) == EVM_OK &&
        r.candidate_id < EVM_MAX_CANDIDATES) {
      _tally[r.candidate_id]++;
      _vm.votes_processed++;
    }
  }
}

void vote_manager_recover_from_storage(void) { vote_manager_init(); }

EvmResult vote_manager_process(uint32_t vote_id, uint8_t candidate_id,
                               uint32_t ts) {
  if (vote_id == 0) {
    logger_log(LOG_VOTE_REJECTED, ts, vote_id);
    Serial.print("VOTE_REJECTED ID=");
    Serial.println(vote_id);
    return EVM_ERR_INVALID;
  }

  if (vote_manager_is_duplicate(vote_id)) {
    _vm.duplicates_rejected++;
    logger_log(LOG_VOTE_REJECTED, ts, vote_id);
    Serial.print("VOTE_REJECTED ID=");
    Serial.println(vote_id);
    return EVM_ERR_DUPLICATE;
  }

  if (candidate_id >= _vm.candidate_count) {
    logger_log(LOG_VOTE_REJECTED, ts, candidate_id);
    Serial.print("VOTE_REJECTED ID=");
    Serial.println(vote_id);
    return EVM_ERR_INVALID;
  }

  VoteRecord rec;
  memset(&rec, 0, sizeof(rec));
  rec.vote_id = vote_id;
  rec.candidate_id = candidate_id;
  rec.timestamp_ms = ts;
  rec.crc = evm_crc16((const uint8_t*)&rec, offsetof(VoteRecord, crc));

  EvmResult r = storage_append_vote(&rec);
  if (r != EVM_OK) {
    logger_log(LOG_ERROR, ts, (uint32_t)r);
    return r;
  }

  _vm.last_vote_id = vote_id;
  _vm.votes_processed++;
  _tally[candidate_id]++;
  logger_log(LOG_VOTE_ACCEPTED, ts, vote_id);
  Serial.print("VOTE_ACCEPTED ID=");
  Serial.print(vote_id);
  Serial.print(" CANDIDATE=");
  Serial.println(candidate_id);
  return EVM_OK;
}

bool vote_manager_is_duplicate(uint32_t vote_id) {
  if (vote_id == 0) return true;

  uint32_t n = storage_get_record_count();
  for (uint32_t i = 0; i < n; i++) {
    VoteRecord r;
    if (storage_read_record(i, &r) == EVM_OK && r.vote_id == vote_id) {
      return true;
    }
  }
  return false;
}

uint32_t vote_manager_get_count(uint8_t candidate_id) {
  if (candidate_id >= EVM_MAX_CANDIDATES) return 0;
  return _tally[candidate_id];
}

uint32_t vote_manager_get_total(void) { return _vm.votes_processed; }
uint32_t vote_manager_get_last_vote_id(void) { return _vm.last_vote_id; }
const VoteManagerState* vote_manager_get_state(void) { return &_vm; }