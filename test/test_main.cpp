#include "stubs/Arduino.h"
#include "stubs/unity.h"

#include "event_manager.h"
#include "evm_events.h"
#include "evm_types.h"
#include "logger.h"
#include "storage_manager.h"
#include "supervisor.h"
#include "tamper_manager.h"
#include "uart_parser.h"
#include "vote_manager.h"

#include <stdio.h>

static const char* ev_name(EventType t) {
  switch (t) {
    case EVT_NONE: return "EVT_NONE";
    case EVT_VOTE: return "EVT_VOTE";
    case EVT_TAMPER: return "EVT_TAMPER";
    case EVT_START: return "EVT_START";
    case EVT_END: return "EVT_END";
    case EVT_REPORT: return "EVT_REPORT";
    case EVT_RESET: return "EVT_RESET";
    case EVT_FRAME_ERROR: return "EVT_FRAME_ERROR";
    case EVT_TIMER_WATCHDOG: return "EVT_TIMER_WATCHDOG";
    case EVT_TIMER_TAMPER_POLL: return "EVT_TIMER_TAMPER_POLL";
    case EVT_TIMER_TICK: return "EVT_TIMER_TICK";
    default: return "EVT_UNKNOWN";
  }
}

static const char* res_name(EvmResult r) {
  switch (r) {
    case EVM_OK: return "EVM_OK";
    case EVM_ERR_DUPLICATE: return "EVM_ERR_DUPLICATE";
    case EVM_ERR_INVALID: return "EVM_ERR_INVALID";
    case EVM_ERR_WRONG_STATE: return "EVM_ERR_WRONG_STATE";
    case EVM_ERR_FULL: return "EVM_ERR_FULL";
    case EVM_ERR_STORAGE_FAIL: return "EVM_ERR_STORAGE_FAIL";
    case EVM_ERR_CRC: return "EVM_ERR_CRC";
    default: return "EVM_ERR_UNKNOWN";
  }
}

static void print_event_payload(const ParsedEvent* e) {
  printf("    event=%s ts=%lu", ev_name(e->type), (unsigned long)e->timestamp_ms);
  if (e->type == EVT_VOTE) {
    printf(" vote_id=%lu candidate=%u",
           (unsigned long)e->data.vote.vote_id,
           (unsigned)e->data.vote.candidate_id);
  } else if (e->type == EVT_TAMPER) {
    printf(" tamper_flags=0x%lx", (unsigned long)e->data.tamper.tamper_flags);
  }
  printf("\n");
}

static bool trace_uart_decode(const char* test_name, const char* frame, ParsedEvent* out_evt) {
  bool got = false;
  uart_parser_init();
  printf("  [%s] UART frame: %s\n", test_name, frame);

  for (uint32_t i = 0; frame[i] != '\0'; ++i) {
    char ch = frame[i];
    bool step = uart_parser_feed((uint8_t)ch, out_evt);
    const ParserState* ps = uart_parser_get_state();
    printf("    byte[%lu]='%c' (0x%02X) state=%u len=%u frame_count=%lu err_count=%lu decoded=%u\n",
           (unsigned long)i,
           (ch >= 32 && ch <= 126) ? ch : '.',
           (unsigned)(uint8_t)ch,
           (unsigned)ps->state,
           (unsigned)ps->len,
           (unsigned long)ps->frame_count,
           (unsigned long)ps->error_count,
           (unsigned)step);
    got |= step;
  }

  if (got) {
    printf("  [%s] Decoded event:\n", test_name);
    print_event_payload(out_evt);
  } else {
    printf("  [%s] Decode failed\n", test_name);
  }
  return got;
}

static EvmResult route_to_supervisor_traced(const ParsedEvent* ev) {
  printf("  [DISPATCH] handler input:\n");
  print_event_payload(ev);
  EvmResult r = supervisor_handle_event(ev);
  printf("  [DISPATCH] handler result=%s state=%s transitions=%lu\n",
         res_name(r),
         supervisor_state_name(supervisor_get_state()),
         (unsigned long)supervisor_get_transition_count());
  return r;
}

static void trace_enqueue_and_dispatch(const char* step_name, ParsedEvent* e) {
  uint32_t depth_before = event_manager_get_queue_depth();
  uint32_t dropped_before = event_manager_get_dropped_count();

  printf("  [%s] enqueue begin depth=%lu dropped=%lu\n",
         step_name, (unsigned long)depth_before, (unsigned long)dropped_before);
  print_event_payload(e);

  bool ok = event_manager_enqueue(e);
  printf("  [%s] enqueue ok=%u depth_now=%lu\n",
         step_name, (unsigned)ok, (unsigned long)event_manager_get_queue_depth());

  TEST_ASSERT_TRUE(ok);

  uint32_t dispatched = event_manager_dispatch_all();
  printf("  [%s] dispatch count=%lu depth_after=%lu dropped_after=%lu\n",
         step_name,
         (unsigned long)dispatched,
         (unsigned long)event_manager_get_queue_depth(),
         (unsigned long)event_manager_get_dropped_count());
}

static EvmResult trace_direct_handle(const char* step_name, ParsedEvent* e) {
  printf("  [%s] direct handle input:\n", step_name);
  print_event_payload(e);
  EvmResult r = supervisor_handle_event(e);
  printf("  [%s] direct handle result=%s state=%s\n",
         step_name, res_name(r), supervisor_state_name(supervisor_get_state()));
  return r;
}

static void trace_logs(const char* tag) {
  printf("  [%s] logger buffered entries=%lu\n", tag, (unsigned long)logger_get_count());
  for (uint32_t i = 0; i < logger_get_count(); ++i) {
    LogEntry le;
    if (logger_get_entry(i, &le)) {
      printf("    log[%lu] type=%u ts=%lu data=%lu\n",
             (unsigned long)i,
             (unsigned)le.type,
             (unsigned long)le.timestamp_ms,
             (unsigned long)le.data);
    }
  }
}

static void reset_all() {
  g_ms = 0;
  arduino_stub_reset_pins();

  logger_init();
  storage_reset();
  storage_init();
  vote_manager_init();
  tamper_manager_init();
  uart_parser_init();

  // event_manager_init() must come before supervisor_init() — the handler
  // table must be ready before the state machine makes its first transition.
  event_manager_init();
  event_manager_register_handler(EVT_VOTE,              route_to_supervisor_traced);
  event_manager_register_handler(EVT_TAMPER,            route_to_supervisor_traced);
  event_manager_register_handler(EVT_START,             route_to_supervisor_traced);
  event_manager_register_handler(EVT_END,               route_to_supervisor_traced);
  event_manager_register_handler(EVT_REPORT,            route_to_supervisor_traced);
  event_manager_register_handler(EVT_RESET,             route_to_supervisor_traced);
  event_manager_register_handler(EVT_FRAME_ERROR,       route_to_supervisor_traced);
  event_manager_register_handler(EVT_TIMER_WATCHDOG,    route_to_supervisor_traced);
  event_manager_register_handler(EVT_TIMER_TAMPER_POLL, route_to_supervisor_traced);
  event_manager_register_handler(EVT_TIMER_TICK,        route_to_supervisor_traced);

  supervisor_init(RESET_COLD_BOOT);
}

void setUp(void) { reset_all(); }
void tearDown(void) {}

static void enqueue_and_dispatch(ParsedEvent* e) {
  trace_enqueue_and_dispatch("enqueue_and_dispatch", e);
}

// ── T-1: Sequential duplicate vote rejected ───────────────────────────────────
void test_duplicate_vote(void) {
  ParsedEvent e = EVT_EMPTY;
  e.type = EVT_START;
  enqueue_and_dispatch(&e);
  TEST_ASSERT_EQUAL(STATE_VOTING_ACTIVE, supervisor_get_state());

  e = EVT_EMPTY;
  e.type = EVT_VOTE;
  e.data.vote.vote_id = 1;
  e.data.vote.candidate_id = 0;
  e.timestamp_ms = 100;
  enqueue_and_dispatch(&e);

  e.data.vote.vote_id = 1;
  e.timestamp_ms = 110;
  enqueue_and_dispatch(&e);

  TEST_ASSERT_EQUAL_UINT32(1, vote_manager_get_total());
  trace_logs("test_duplicate_vote");
}

// ── T-2: Non-sequential duplicate rejected (bug 4 regression test) ────────────
void test_nonsequential_duplicate(void) {
  ParsedEvent e = EVT_EMPTY;
  e.type = EVT_START;
  enqueue_and_dispatch(&e);

  e = EVT_EMPTY;
  e.type = EVT_VOTE;
  e.data.vote.vote_id = 1;
  e.data.vote.candidate_id = 0;
  e.timestamp_ms = 100;
  enqueue_and_dispatch(&e);

  e.data.vote.vote_id = 2;
  e.data.vote.candidate_id = 1;
  e.timestamp_ms = 110;
  enqueue_and_dispatch(&e);

  e.data.vote.vote_id = 1;
  e.data.vote.candidate_id = 0;
  e.timestamp_ms = 120;
  enqueue_and_dispatch(&e);

  TEST_ASSERT_EQUAL_UINT32(2, vote_manager_get_total());
  TEST_ASSERT_EQUAL_UINT32(1, vote_manager_get_count(0));
  TEST_ASSERT_EQUAL_UINT32(1, vote_manager_get_count(1));
  trace_logs("test_nonsequential_duplicate");
}

// ── T-3: Vote rejected in wrong state ────────────────────────────────────────
void test_wrong_state_vote(void) {
  ParsedEvent evt = EVT_EMPTY;
  evt.type = EVT_VOTE;
  evt.data.vote.vote_id = 42;
  evt.data.vote.candidate_id = 1;
  evt.timestamp_ms = 200;
  TEST_ASSERT_EQUAL(EVM_ERR_WRONG_STATE, trace_direct_handle("test_wrong_state_vote", &evt));
  trace_logs("test_wrong_state_vote");
}

void test_tamper_detection(void) {
  ParsedEvent evt = EVT_EMPTY;
  evt.type = EVT_TAMPER;
  evt.data.tamper.tamper_flags = TAMPER_CASE_OPEN;
  TEST_ASSERT_EQUAL(EVM_OK, trace_direct_handle("test_tamper_detection", &evt));
  TEST_ASSERT_EQUAL(STATE_TAMPER_DETECTED, supervisor_get_state());
  trace_logs("test_tamper_detection");
}

// ── T-5: No votes accepted after tamper ──────────────────────────────────────
void test_vote_after_tamper(void) {
  supervisor_force_tamper_lockdown(TAMPER_VOLTAGE);
  ParsedEvent evt = EVT_EMPTY;
  evt.type = EVT_VOTE;
  evt.data.vote.vote_id = 6;
  evt.data.vote.candidate_id = 0;
  TEST_ASSERT_EQUAL(EVM_ERR_WRONG_STATE, trace_direct_handle("test_vote_after_tamper", &evt));
  trace_logs("test_vote_after_tamper");
}

void test_malformed_frame(void) {
  ParsedEvent evt = EVT_EMPTY;
  bool got = trace_uart_decode("test_malformed_frame", "VOTE,ID=1,CANDIDATE=0,TS=100*", &evt);
  TEST_ASSERT_FALSE(got);
}

// ── FT-2: Valid VOTE frame parsed correctly ───────────────────────────────────
void test_good_frame(void) {
  ParsedEvent evt = EVT_EMPTY;
  bool got = trace_uart_decode("test_good_frame", "$VOTE,ID=7,CANDIDATE=2,TS=500*", &evt);
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL(EVT_VOTE, evt.type);
  TEST_ASSERT_EQUAL_UINT32(7, evt.data.vote.vote_id);
  TEST_ASSERT_EQUAL_UINT32(2, evt.data.vote.candidate_id);
}

// ── FT-3: CTRL START frame parsed correctly ───────────────────────────────────
void test_ctrl_start_frame(void) {
  ParsedEvent evt = EVT_EMPTY;
  bool got = trace_uart_decode("test_ctrl_start_frame", "$CTRL,CMD=START*", &evt);
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL(EVT_START, evt.type);
}

// ── FT-4: Valid TAMPER frame parsed correctly ────────────────────────────────
void test_tamper_frame(void) {
  ParsedEvent evt = EVT_EMPTY;
  bool got = trace_uart_decode("test_tamper_frame", "$TAMPER,TYPE=2,TS=700*", &evt);
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL(EVT_TAMPER, evt.type);
  TEST_ASSERT_EQUAL_UINT32(2, evt.data.tamper.tamper_flags);
  TEST_ASSERT_EQUAL_UINT32(700, evt.timestamp_ms);
}

// ── FT-5: Unknown CTRL command is rejected ───────────────────────────────────
void test_unknown_ctrl_rejected(void) {
  ParsedEvent evt = EVT_EMPTY;
  bool got = trace_uart_decode("test_unknown_ctrl_rejected", "$CTRL,CMD=PAUSE*", &evt);
  TEST_ASSERT_FALSE(got);
  TEST_ASSERT_EQUAL(EVT_NONE, evt.type);
}

// ── FT-6: VOTE missing CANDIDATE field is rejected ───────────────────────────
void test_vote_missing_candidate_rejected(void) {
  ParsedEvent evt = EVT_EMPTY;
  bool got = trace_uart_decode("test_vote_missing_candidate_rejected", "$VOTE,ID=9,TS=123*", &evt);
  TEST_ASSERT_FALSE(got);
  TEST_ASSERT_EQUAL(EVT_NONE, evt.type);
}

// ── FT-7: Oversized frame increments parser error count ──────────────────────
void test_oversized_frame_increments_error(void) {
  uart_parser_init();
  ParsedEvent evt = EVT_EMPTY;
  uint32_t before = uart_parser_get_error_count();

  TEST_ASSERT_FALSE(uart_parser_feed((uint8_t)'$', &evt));
  for (int i = 0; i < 140; i++) {
    bool step = uart_parser_feed((uint8_t)'A', &evt);
    const ParserState* ps = uart_parser_get_state();
    printf("    oversized byte[%d] state=%u len=%u err_count=%lu decoded=%u\n",
           i, (unsigned)ps->state, (unsigned)ps->len,
           (unsigned long)ps->error_count, (unsigned)step);
    TEST_ASSERT_FALSE(step);
  }
  TEST_ASSERT_FALSE(uart_parser_feed((uint8_t)'*', &evt));

  uint32_t after = uart_parser_get_error_count();
  TEST_ASSERT_TRUE(after > before);
}

// ── ST-1: Full election flow ──────────────────────────────────────────────────
void test_full_election_flow(void) {
  ParsedEvent e = EVT_EMPTY;
  e.type = EVT_START;
  enqueue_and_dispatch(&e);
  TEST_ASSERT_EQUAL(STATE_VOTING_ACTIVE, supervisor_get_state());

  e = EVT_EMPTY;
  e.type = EVT_VOTE;
  e.data.vote.vote_id = 1;
  e.data.vote.candidate_id = 0;
  e.timestamp_ms = 100;
  enqueue_and_dispatch(&e);

  e.data.vote.vote_id = 2;
  e.data.vote.candidate_id = 1;
  e.timestamp_ms = 110;
  enqueue_and_dispatch(&e);

  e.data.vote.vote_id = 3;
  e.data.vote.candidate_id = 0;
  e.timestamp_ms = 120;
  enqueue_and_dispatch(&e);

  e = EVT_EMPTY;
  e.type = EVT_END;
  enqueue_and_dispatch(&e);
  TEST_ASSERT_EQUAL(STATE_VOTING_CLOSED, supervisor_get_state());

  TEST_ASSERT_EQUAL_UINT32(2, vote_manager_get_count(0));
  TEST_ASSERT_EQUAL_UINT32(1, vote_manager_get_count(1));
  TEST_ASSERT_EQUAL_UINT32(3, vote_manager_get_total());
  trace_logs("test_full_election_flow");
}

// ── ST-2: EVT_REPORT rejected outside VOTING_CLOSED ──────────────────────────
void test_report_wrong_state(void) {
  ParsedEvent e = EVT_EMPTY;
  e.type = EVT_REPORT;
  TEST_ASSERT_EQUAL(EVM_ERR_WRONG_STATE, trace_direct_handle("test_report_wrong_state", &e));
  trace_logs("test_report_wrong_state");
}

void test_generate_tamper_direct(void) {
  ParsedEvent e = EVT_EMPTY;
  e.type = EVT_TAMPER;
  e.data.tamper.tamper_flags = TAMPER_CASE_OPEN;
  e.timestamp_ms = 1000;
  enqueue_and_dispatch(&e);
  TEST_ASSERT_EQUAL(STATE_TAMPER_DETECTED, supervisor_get_state());
  trace_logs("test_generate_tamper_direct");
}

// Generate tamper event via UART and verify supervisor enters lockdown state
void test_generate_tamper_via_uart(void) {
  ParsedEvent evt = EVT_EMPTY;
  bool got = trace_uart_decode("test_generate_tamper_via_uart", "$TAMPER,TYPE=1,TS=700*", &evt);
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL(EVT_TAMPER, evt.type);

  enqueue_and_dispatch(&evt);
  TEST_ASSERT_EQUAL(STATE_TAMPER_DETECTED, supervisor_get_state());
  trace_logs("test_generate_tamper_via_uart");
}

static uint32_t g_reset_handler_calls = 0;

static EvmResult test_reset_handler(const ParsedEvent* ev) {
  printf("  [test_reset_handler] called with %s\n", ev_name(ev->type));
  g_reset_handler_calls++;
  return EVM_OK;
}

void test_reset_ctrl_frame_dispatches_reset_handler(void) {
  ParsedEvent evt = EVT_EMPTY;
  bool got = trace_uart_decode("test_reset_ctrl_frame_dispatches_reset_handler", "$CTRL,CMD=RESET*", &evt);

  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL(EVT_RESET, evt.type);

  g_reset_handler_calls = 0;
  event_manager_register_handler(EVT_RESET, test_reset_handler);

  TEST_ASSERT_TRUE(event_manager_enqueue(&evt));
  uint32_t dispatched = event_manager_dispatch_all();
  printf("  [test_reset_ctrl_frame_dispatches_reset_handler] dispatch_count=%lu\n", (unsigned long)dispatched);

  TEST_ASSERT_EQUAL_UINT32(1, g_reset_handler_calls);
}

void test_invalid_event_type_rejected_on_enqueue(void) {
  ParsedEvent e = EVT_EMPTY;
  e.type = (EventType)EVT_MAX;
  uint32_t dropped_before = event_manager_get_dropped_count();

  bool ok = event_manager_enqueue(&e);
  printf("  [test_invalid_event_type_rejected_on_enqueue] enqueue_ok=%u dropped_before=%lu dropped_after=%lu\n",
         (unsigned)ok,
         (unsigned long)dropped_before,
         (unsigned long)event_manager_get_dropped_count());

  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT32(dropped_before + 1, event_manager_get_dropped_count());
}

void test_invalid_timer_event_type_rejected(void) {
  uint32_t dropped_before = event_manager_get_dropped_count();

  bool ok = event_manager_enqueue_timer((EventType)99, millis(), 0);
  printf("  [test_invalid_timer_event_type_rejected] enqueue_ok=%u dropped_before=%lu dropped_after=%lu\n",
         (unsigned)ok,
         (unsigned long)dropped_before,
         (unsigned long)event_manager_get_dropped_count());

  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT32(dropped_before + 1, event_manager_get_dropped_count());
}

void test_log_overflow_tracks_persist_drops(void) {
  logger_init();
  storage_reset();
  storage_init();

  uint32_t capacity = storage_get_log_capacity();
  uint32_t extra = 10;

  for (uint32_t i = 0; i < capacity + extra; i++) {
    logger_log(LOG_ADMIN_CMD, 1000 + i, i);
  }
  logger_flush();

  printf("  [test_log_overflow_tracks_persist_drops] persisted=%lu capacity=%lu dropped=%lu\n",
         (unsigned long)storage_get_log_count(),
         (unsigned long)storage_get_log_capacity(),
         (unsigned long)logger_get_persist_dropped());

  TEST_ASSERT_EQUAL_UINT32(capacity, storage_get_log_count());
  TEST_ASSERT_EQUAL_UINT32(extra, logger_get_persist_dropped());
}

void test_tamper_combined_mask_parsed(void) {
  ParsedEvent evt = EVT_EMPTY;
  bool got = trace_uart_decode("test_tamper_combined_mask_parsed", "$TAMPER,TYPE=3,TS=700*", &evt);
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL(EVT_TAMPER, evt.type);
  TEST_ASSERT_EQUAL_UINT32(3, evt.data.tamper.tamper_flags);
}

void test_tamper_invalid_mask_rejected(void) {
  ParsedEvent evt = EVT_EMPTY;
  bool got = trace_uart_decode("test_tamper_invalid_mask_rejected", "$TAMPER,TYPE=16,TS=700*", &evt);
  TEST_ASSERT_FALSE(got);
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_duplicate_vote);
  RUN_TEST(test_nonsequential_duplicate);
  RUN_TEST(test_wrong_state_vote);
  RUN_TEST(test_tamper_detection);
  RUN_TEST(test_vote_after_tamper);
  RUN_TEST(test_malformed_frame);
  RUN_TEST(test_good_frame);
  RUN_TEST(test_ctrl_start_frame);
  RUN_TEST(test_tamper_frame);
  RUN_TEST(test_unknown_ctrl_rejected);
  RUN_TEST(test_vote_missing_candidate_rejected);
  RUN_TEST(test_oversized_frame_increments_error);
  RUN_TEST(test_full_election_flow);
  RUN_TEST(test_report_wrong_state);
  RUN_TEST(test_generate_tamper_direct);
  RUN_TEST(test_generate_tamper_via_uart);
  RUN_TEST(test_reset_ctrl_frame_dispatches_reset_handler);
  RUN_TEST(test_invalid_event_type_rejected_on_enqueue);
  RUN_TEST(test_invalid_timer_event_type_rejected);
  RUN_TEST(test_log_overflow_tracks_persist_drops);
  RUN_TEST(test_tamper_combined_mask_parsed);
  RUN_TEST(test_tamper_invalid_mask_rejected);
  return UNITY_END();
}

