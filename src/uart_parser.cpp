#include <Arduino.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "uart_parser.h"
#include "logger.h"

// Protocol: $BODY*
static ParserState _ps;

static bool _decode_frame(const char* body, EvmEvent* evt);

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
  memset(&_ps, 0, sizeof(_ps));
  _ps.state = PARSE_IDLE;
}

bool uart_parser_feed(uint8_t byte, ParsedEvent* evt) {
  char c = (char)byte;

  switch (_ps.state) {
    case PARSE_IDLE:
      if (c == FRAME_START) {
        _ps.len = 0;
        _ps.state = PARSE_BODY;
      }
      return false;

    case PARSE_BODY:
      if (c == FRAME_END) {
        _ps.buf[_ps.len] = '\0';
        _ps.state = PARSE_IDLE;
        _ps.frame_count++;

        bool ok = _decode_frame((const char*)_ps.buf, evt);
        if (!ok) {
          _ps.error_count++;
          logger_log(LOG_ERROR, millis(), 0xF002);
        }
        return ok;
      }

      if (c == '\n' || c == '\r') {
        return false;
      }

      if (_ps.len >= EVM_FRAME_MAX_SIZE - 1) {
        _ps.error_count++;
        logger_log(LOG_ERROR, millis(), 0xF001);
        _ps.state = PARSE_IDLE;
        _ps.len = 0;
        return false;
      }

      _ps.buf[_ps.len++] = byte;
      return false;

    default:
      _ps.state = PARSE_IDLE;
      return false;
  }
}

static bool _decode_frame(const char* body, EvmEvent* evt) {
  if (!body || !evt) return false;
  *evt = EVT_EMPTY;
  evt->timestamp_ms = millis();

  if (strncmp(body, "VOTE,", 5) == 0) {
    int32_t id = _get_field(body, "ID");
    int32_t can = _get_field(body, "CANDIDATE");
    int32_t ts = _get_field(body, "TS");
    if (id < 0 || can < 0) return false;

    evt->type = EVT_VOTE;
    evt->data.vote.vote_id = (uint32_t)id;
    evt->data.vote.candidate_id = (uint8_t)can;
    evt->timestamp_ms = (ts >= 0) ? (uint32_t)ts : millis();
    return true;
  }

  if (strncmp(body, "TAMPER,", 7) == 0) {
    int32_t type = _get_field(body, "TYPE");
    int32_t ts = _get_field(body, "TS");
    if (type <= 0) return false;

    const uint32_t allowed = (uint32_t)TAMPER_CASE_OPEN |
                             (uint32_t)TAMPER_VOLTAGE | (uint32_t)TAMPER_CLOCK;

    uint32_t flags = (uint32_t)type;
    if ((flags & ~allowed) != 0) return false;

    evt->type = EVT_TAMPER;
    evt->data.tamper.tamper_flags = flags;
    evt->timestamp_ms = (ts >= 0) ? (uint32_t)ts : millis();
    return true;
  }

  if (strncmp(body, "CTRL,CMD=", 9) == 0) {
    const char* cmd = body + 9;
    if (strcmp(cmd, "START") == 0) {
      evt->type = EVT_START;
      return true;
    }
    if (strcmp(cmd, "END") == 0) {
      evt->type = EVT_END;
      return true;
    }
    if (strcmp(cmd, "REPORT") == 0) {
      evt->type = EVT_REPORT;
      return true;
    }
    if (strcmp(cmd, "RESET") == 0) {
      evt->type = EVT_RESET;
      return true;
    }
    if (strcmp(cmd, "STATUS") == 0) {
      evt->type = EVT_STATUS;
      return true;
    }
  }

  return false;
}

void uart_parser_reset(void) {
  _ps.len = 0;
  _ps.state = PARSE_IDLE;
}

const ParserState* uart_parser_get_state(void) { return &_ps; }

uint32_t uart_parser_get_error_count(void) { return _ps.error_count; }