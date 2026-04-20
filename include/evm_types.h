#pragma once
#include <stdint.h>
#include <stdbool.h>

// ─── Election State ───────────────────────────────────────────────────────────
typedef enum {
    STATE_INITIALIZATION   = 0,
    STATE_PRE_ELECTION     = 1,
    STATE_VOTING_ACTIVE    = 2,
    STATE_VOTING_CLOSED    = 3,
    STATE_TAMPER_DETECTED  = 4,
    STATE_ERROR            = 5,
    STATE_COUNT            = 6
} ElectionState;

// ─── Reset Cause ─────────────────────────────────────────────────────────────
typedef enum {
    RESET_COLD_BOOT    = 0,
    RESET_POWER_LOSS   = 1,
    RESET_WATCHDOG     = 2,
    RESET_SOFTWARE     = 3,
    RESET_UNKNOWN      = 4
} ResetCause;

// ─── Tamper Type ─────────────────────────────────────────────────────────────

typedef enum {
    TAMPER_CASE_OPEN  = (1u << 0),
    TAMPER_VOLTAGE    = (1u << 1),
    TAMPER_CLOCK      = (1u << 2)
} TamperType;

// ─── EVM Result Codes ────────────────────────────────────────────────────────

typedef enum {
    EVM_OK               = 0,
    EVM_ERR_DUPLICATE    = 1,
    EVM_ERR_INVALID      = 2,
    EVM_ERR_WRONG_STATE  = 3,
    EVM_ERR_FULL         = 4,
    EVM_ERR_STORAGE_FAIL = 5,
    EVM_ERR_CRC          = 6
} EvmResult;

// ─── Log Event Types ─────────────────────────────────────────────────────────

typedef enum {
    LOG_STATE_CHANGE  = 0,
    LOG_VOTE_ACCEPTED = 1,
    LOG_VOTE_REJECTED = 2,
    LOG_TAMPER        = 3,
    LOG_RESET         = 4,
    LOG_ERROR         = 5,
    LOG_ADMIN_CMD     = 6
} LogEventType;

// ─── Log Entry ───────────────────────────────────────────────────────────────

typedef struct {
    LogEventType type;
    uint32_t     timestamp_ms;
    uint32_t     data;
} LogEntry;

// ─── Vote Record ─────────────────────────────────────────────────────────────
typedef struct __attribute__((packed)) {
    uint32_t vote_id;
    uint8_t  candidate_id;
    uint32_t timestamp_ms;
    uint16_t crc;
} VoteRecord;

// ─── Tamper Record ───────────────────────────────────────────────────────────

typedef struct __attribute__((packed)) {
    TamperType type;
    uint32_t   timestamp_ms;
    uint16_t   crc;
} TamperRecord;

// ─── Supervisor State ────────────────────────────────────────────────────────

typedef struct {
    ElectionState current_state;
    ElectionState previous_state;
    uint32_t      transition_count;
    uint32_t      last_transition_ms;
    bool          transition_in_progress;
} SupervisorState;

// ─── Vote Manager State ──────────────────────────────────────────────────────

typedef struct {
    uint8_t  candidate_count;
    uint32_t votes_processed;
    uint32_t duplicates_rejected;
    uint32_t last_vote_id;
} VoteManagerState;

// ─── Tamper Manager State ────────────────────────────────────────────────────

typedef struct {
    uint32_t active_flags;
    uint32_t tamper_count;
    uint32_t first_tamper_ms;
    uint32_t last_tamper_ms;
} TamperManagerState;

// ─── Parser State ────────────────────────────────────────────────────────────

#define EVM_FRAME_MAX_SIZE  128

typedef enum {
    PARSE_IDLE = 0,
    PARSE_BODY = 1
} ParserStateEnum;

typedef struct {
    ParserStateEnum state;
    uint8_t  buf[EVM_FRAME_MAX_SIZE];
    uint8_t  len;
    uint32_t frame_count;
    uint32_t error_count;
} ParserState;

// ─── Config ───────────────────────────────────────────────────────────────────

#define EVM_CANDIDATE_COUNT   4
#define MAX_CANDIDATES        EVM_CANDIDATE_COUNT
#define EVM_MAX_CANDIDATES    EVM_CANDIDATE_COUNT

// 200 vote capacity fits a small polling-booth scenario on 32 KB SAMD21 SRAM.
// Each VoteRecord is 11 bytes → 200 records = 2.2 KB, leaving plenty of RAM
// headroom alongside the event queue, log buffer and stack.
#define EVM_MAX_VOTES         200

#define NVM_BASE_ADDRESS      0
#define NVM_MAX_RECORDS       EVM_MAX_VOTES

// 64 entries × 12 bytes each = 768 bytes of RAM for the audit log.
// Sized to hold at least one entry per event type across a typical session
// without consuming more than ~1 KB of the 32 KB SAMD21 SRAM.
#define EVM_LOG_MAX_ENTRIES   64
#define LOG_BUFFER_SIZE       EVM_LOG_MAX_ENTRIES

// 100 ms debounce on tamper GPIO lines.
// Mechanical switches typically settle within 10–50 ms; 100 ms gives a 2×
// safety margin while still responding within one TAMPER_POLL_PERIOD_MS cycle
// (20 ms × 5 = 100 ms worst-case detection latency).
#define EVM_DEBOUNCE_MS       100
#define DEBOUNCE_MS           EVM_DEBOUNCE_MS

// Software watchdog timeout: 2000 ms.
//
// Rationale for 2000 ms:
//   The SAMD21 hardware WDT maximum period is 16 384 ms.  A software watchdog
//   set tighter than the hardware WDT ensures the firmware can perform an
//   orderly reset (flush log, set reset cause) before the hardware WDT fires.
//   2000 ms is 10× the WDT_CHECK_PERIOD_MS (200 ms), giving up to 10 missed
//   check events before a reset is triggered — enough tolerance for brief
//   dispatch spikes without masking genuine firmware stalls.

#define EVM_SOFT_WDT_TIMEOUT_MS   2000
// Legacy alias — do not add new uses; kept for any out-of-tree references.
#define WATCHDOG_TIMEOUT_MS       EVM_SOFT_WDT_TIMEOUT_MS

#define FRAME_MAX_SIZE        EVM_FRAME_MAX_SIZE

// ─── Hardware Pin Assignments ────────────────────────────────────────────────
#define TAMPER_PIN_CASE       2
#define TAMPER_PIN_VOLTAGE    3
#define TAMPER_PIN_CLOCK      4

// ─── Frame Protocol ──────────────────────────────────────────────────────────
#define FRAME_START  '$'
#define FRAME_END    '*'

// ─── CRC-16 utility ──────────────────────────────────────────────────────────
#ifdef __cplusplus
extern "C" {
#endif
uint16_t evm_crc16(const uint8_t* data, uint16_t length);
#ifdef __cplusplus
}
#endif