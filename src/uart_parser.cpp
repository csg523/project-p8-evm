#include "uart_parser.h"
#include "logger.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>

// ─────────────────────────────────────────────
//  UART Parser – byte-by-byte frame assembly
//  Protocol: $BODY*  (start=$, end=*)
// ─────────────────────────────────────────────

// Global parser state: tracks FSM state, buffer, and frame statistics
static ParserState _ps;
/*typedef struct {
    ParserStateEnum state;      // Current FSM state (PARSE_IDLE or PARSE_BODY)
    uint8_t  buf[EVM_FRAME_MAX_SIZE];  // Frame buffer (between $ and *)
    uint8_t  len;               // Current buffer position (number of bytes collected)
    uint32_t frame_count;       // Total frames received
    uint32_t error_count;       // Total decode/overflow errors
} ParserState;*/

// Forward declaration of frame decoder
static bool _decode_frame(const char* body, EvmEvent* evt);

// ─── Helper: Extract integer value from key=value patterns ───────────────────
// Searches for "key=" in body string and returns the integer value following it.
// Returns -1 if key not found or value is malformed.
// Example: _get_field("ID=42,CANDIDATE=2", "ID") → 42
static int32_t _get_field(const char* body, const char* key) {
    if (!body || !key) return -1;
    size_t klen = strlen(key);

    const char* p = body;
    while ((p = strstr(p, key)) != NULL) {
        bool token_start = (p == body) || (*(p - 1) == ',');
        if (token_start && p[klen] == '=') {
            const char* v = p + klen + 1;
            errno = 0;
            char* endptr = NULL;
            long parsed = strtol(v, &endptr, 10);

            if (endptr == v) return -1;
            if (*endptr != '\0' && *endptr != ',') return -1;
            if (errno == ERANGE || parsed < 0 || parsed > INT32_MAX) return -1;

            return (int32_t)parsed;
        }
        p += 1;
    }
    return -1;
}

void uart_parser_init(void) {
    // Initialize parser state: clear all fields to zero
    memset(&_ps, 0, sizeof(_ps));
    // Set initial state to PARSE_IDLE (waiting for frame start '$')
    _ps.state = PARSE_IDLE;
}

bool uart_parser_feed(uint8_t byte, ParsedEvent* evt) {
    char c = (char)byte;

    switch (_ps.state) {
        case PARSE_IDLE:
            // Waiting for frame start marker '$'
            if (c == FRAME_START) {
                _ps.len = 0;                // Reset buffer position
                _ps.state = PARSE_BODY;     // Transition to body collection state
            }
            return false;                   // No complete frame yet

        case PARSE_BODY:
            // Collecting frame data between '$' and '*'
            if (c == FRAME_END) {
                // Frame complete: null-terminate, decode, and return result
                _ps.buf[_ps.len] = '\0';    // Null-terminate the buffer
                _ps.state = PARSE_IDLE;     // Reset to waiting for next frame
                _ps.frame_count++;          // Increment successful frame count

                bool ok = _decode_frame((const char*)_ps.buf, evt);
                if (!ok) {
                    _ps.error_count++;      // Track decode errors
                    logger_log(LOG_ERROR, millis(), 0xF002);
                }
                return ok;                  // Return true only if frame decoded successfully
            }

            if (c == '\n' || c == '\r') {
                // Discard stray newlines mid-frame (ignore them)
                return false;
            }

            if (_ps.len >= EVM_FRAME_MAX_SIZE - 1) {
                // Frame too long – discard and reset to prevent buffer overflow
                _ps.error_count++;
                logger_log(LOG_ERROR, millis(), 0xF001);
                _ps.state = PARSE_IDLE;     // Reset to idle state
                _ps.len = 0;
                return false;
            }

            // Append byte to buffer and increment position
            _ps.buf[_ps.len++] = byte;
            return false;                   // Wait for more data or frame end marker

        default:
            // Unknown state – reset to idle for safety
            _ps.state = PARSE_IDLE;
            return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Decode null-terminated frame body (text between $ and *)
// Protocol examples:
//   "VOTE,ID=42,CANDIDATE=2,TS=999"  → EVT_VOTE
//   "TAMPER,TYPE=1,TS=999"            → EVT_TAMPER
//   "CTRL,CMD=START"                  → EVT_START
// Returns true if frame is valid and evt is populated; false if malformed
// ─────────────────────────────────────────────────────────────────────────────
static bool _decode_frame(const char* body, EvmEvent* evt) {
    if (!body || !evt) return false;
    *evt = EVT_EMPTY;                   // Initialize event to empty
    evt->timestamp_ms = millis();       // Set default timestamp to current time

    // ── VOTE Frame ─────────────────────────────────────────────────────────────
    if (strncmp(body, "VOTE,", 5) == 0) {
        int32_t id  = _get_field(body, "ID");           // Extract vote ID
        int32_t can = _get_field(body, "CANDIDATE");    // Extract candidate ID
        int32_t ts  = _get_field(body, "TS");           // Extract timestamp (optional)
        if (id < 0 || can < 0) return false;            // ID and CANDIDATE are mandatory

        evt->type = EVT_VOTE;
        evt->data.vote.vote_id = (uint32_t)id;
        evt->data.vote.candidate_id = (uint8_t)can;
        evt->timestamp_ms = (ts >= 0) ? (uint32_t)ts : millis();
        return true;
    }

    // ── TAMPER Frame ───────────────────────────────────────────────────────────
    if (strncmp(body, "TAMPER,", 7) == 0) {
        int32_t type = _get_field(body, "TYPE");       // Extract tamper type
        int32_t ts   = _get_field(body, "TS");         // Extract timestamp (optional)
        if (type <= 0) return false;

        const uint32_t allowed =
            (uint32_t)TAMPER_CASE_OPEN |
            (uint32_t)TAMPER_VOLTAGE |
            (uint32_t)TAMPER_CLOCK;

        uint32_t flags = (uint32_t)type;
        if ((flags & ~allowed) != 0) return false;

        evt->type = EVT_TAMPER;
        evt->data.tamper.tamper_flags = flags;
        evt->timestamp_ms = (ts >= 0) ? (uint32_t)ts : millis();
        return true;
    }

    // ── CONTROL Frame ──────────────────────────────────────────────────────────
    if (strncmp(body, "CTRL,CMD=", 9) == 0) {
        const char* cmd = body + 9;     // Extract command string after "CTRL,CMD="
        // Map command string to event type
        if (strcmp(cmd, "START") == 0)  { evt->type = EVT_START;  return true; }
        if (strcmp(cmd, "END") == 0)    { evt->type = EVT_END;    return true; }
        if (strcmp(cmd, "REPORT") == 0) { evt->type = EVT_REPORT; return true; }
        if (strcmp(cmd, "RESET") == 0)  { evt->type = EVT_RESET;  return true; }
    }

    return false;  // Unknown frame type or malformed
}

void uart_parser_reset(void) {
    // Reset parser state: clear buffer and return to idle
    _ps.len = 0;
    _ps.state = PARSE_IDLE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostic/monitoring functions
// ─────────────────────────────────────────────────────────────────────────────

const ParserState* uart_parser_get_state(void) { 
    // Returns pointer to current parser state (for diagnostics)
    return &_ps; 
}

uint32_t uart_parser_get_error_count(void) { 
    // Returns total number of frame decode or overflow errors encountered
    return _ps.error_count; 
}