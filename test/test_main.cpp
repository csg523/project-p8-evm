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

static EvmResult route_to_supervisor(const ParsedEvent* ev) {
  return supervisor_handle_event(ev);
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
  event_manager_register_handler(EVT_VOTE,              route_to_supervisor);
  event_manager_register_handler(EVT_TAMPER,            route_to_supervisor);
  event_manager_register_handler(EVT_START,             route_to_supervisor);
  event_manager_register_handler(EVT_END,               route_to_supervisor);
  event_manager_register_handler(EVT_REPORT,            route_to_supervisor);
  event_manager_register_handler(EVT_RESET,             route_to_supervisor);
  event_manager_register_handler(EVT_FRAME_ERROR,       route_to_supervisor);
  event_manager_register_handler(EVT_TIMER_WATCHDOG,    route_to_supervisor);
  event_manager_register_handler(EVT_TIMER_TAMPER_POLL, route_to_supervisor);
  event_manager_register_handler(EVT_TIMER_TICK,        route_to_supervisor);

  supervisor_init(RESET_COLD_BOOT);
}

void setUp(void)    { reset_all(); }
void tearDown(void) {}

static void enqueue_and_dispatch(ParsedEvent* e) {
  TEST_ASSERT_TRUE(event_manager_enqueue(e));
  event_manager_dispatch_all();
}

// ── T-1: Sequential duplicate vote rejected ───────────────────────────────────
void test_duplicate_vote(void) {
  ParsedEvent e = EVT_EMPTY;
  e.type = EVT_START;
  enqueue_and_dispatch(&e);
  TEST_ASSERT_EQUAL(STATE_VOTING_ACTIVE, supervisor_get_state());

  e = EVT_EMPTY;
  e.type = EVT_VOTE;
  e.data.vote.vote_id = 1; e.data.vote.candidate_id = 0; e.timestamp_ms = 100;
  enqueue_and_dispatch(&e);

  e.data.vote.vote_id = 1; e.timestamp_ms = 110;  // same ID again
  enqueue_and_dispatch(&e);

  TEST_ASSERT_EQUAL_UINT32(1, vote_manager_get_total());
}

// ── T-2: Non-sequential duplicate rejected (bug 4 regression test) ────────────
void test_nonsequential_duplicate(void) {
  ParsedEvent e = EVT_EMPTY;
  e.type = EVT_START;
  enqueue_and_dispatch(&e);

  e = EVT_EMPTY; e.type = EVT_VOTE;
  e.data.vote.vote_id = 1; e.data.vote.candidate_id = 0; e.timestamp_ms = 100;
  enqueue_and_dispatch(&e);

  e.data.vote.vote_id = 2; e.data.vote.candidate_id = 1; e.timestamp_ms = 110;
  enqueue_and_dispatch(&e);

  // Re-submit vote ID 1 — must be rejected even though ID 2 came after it
  e.data.vote.vote_id = 1; e.data.vote.candidate_id = 0; e.timestamp_ms = 120;
  enqueue_and_dispatch(&e);

  TEST_ASSERT_EQUAL_UINT32(2, vote_manager_get_total());
  TEST_ASSERT_EQUAL_UINT32(1, vote_manager_get_count(0));
  TEST_ASSERT_EQUAL_UINT32(1, vote_manager_get_count(1));
}

// ── T-3: Vote rejected in wrong state ────────────────────────────────────────
void test_wrong_state_vote(void) {
  ParsedEvent evt = EVT_EMPTY;
  evt.type = EVT_VOTE;
  evt.data.vote.vote_id = 42; evt.data.vote.candidate_id = 1; evt.timestamp_ms = 200;
  TEST_ASSERT_EQUAL(EVM_ERR_WRONG_STATE, supervisor_handle_event(&evt));
}

// ── T-4: Tamper causes lockdown ───────────────────────────────────────────────
void test_tamper_detection(void) {
  ParsedEvent evt = EVT_EMPTY;
  evt.type = EVT_TAMPER;
  evt.data.tamper.tamper_flags = TAMPER_CASE_OPEN;
  supervisor_handle_event(&evt);
  TEST_ASSERT_EQUAL(STATE_TAMPER_DETECTED, supervisor_get_state());
}

// ── T-5: No votes accepted after tamper ──────────────────────────────────────
void test_vote_after_tamper(void) {
  supervisor_force_tamper_lockdown(TAMPER_VOLTAGE);
  ParsedEvent evt = EVT_EMPTY;
  evt.type = EVT_VOTE;
  evt.data.vote.vote_id = 6; evt.data.vote.candidate_id = 0;
  TEST_ASSERT_EQUAL(EVM_ERR_WRONG_STATE, supervisor_handle_event(&evt));
}

// ── FT-1: Frame without $ is rejected ────────────────────────────────────────
void test_malformed_frame(void) {
  uart_parser_init();
  ParsedEvent evt = EVT_EMPTY;
  const char* bad = "VOTE,ID=1,CANDIDATE=0,TS=100*";
  bool got = false;
  for (const char* c = bad; *c; c++) got |= uart_parser_feed((uint8_t)*c, &evt);
  TEST_ASSERT_FALSE(got);
}

// ── FT-2: Valid VOTE frame parsed correctly ───────────────────────────────────
void test_good_frame(void) {
  uart_parser_init();
  ParsedEvent evt = EVT_EMPTY;
  const char* frame = "$VOTE,ID=7,CANDIDATE=2,TS=500*";
  bool got = false;
  for (const char* c = frame; *c; c++) got |= uart_parser_feed((uint8_t)*c, &evt);
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL(EVT_VOTE, evt.type);
  TEST_ASSERT_EQUAL_UINT32(7, evt.data.vote.vote_id);
  TEST_ASSERT_EQUAL_UINT32(2, evt.data.vote.candidate_id);
}

// ── FT-3: CTRL START frame parsed correctly ───────────────────────────────────
void test_ctrl_start_frame(void) {
  uart_parser_init();
  ParsedEvent evt = EVT_EMPTY;
  const char* frame = "$CTRL,CMD=START*";
  bool got = false;
  for (const char* c = frame; *c; c++) got |= uart_parser_feed((uint8_t)*c, &evt);
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL(EVT_START, evt.type);
}

// ── FT-4: Valid TAMPER frame parsed correctly ────────────────────────────────
void test_tamper_frame(void) {
  uart_parser_init();
  ParsedEvent evt = EVT_EMPTY;
  const char* frame = "$TAMPER,TYPE=2,TS=700*";
  bool got = false;
  for (const char* c = frame; *c; c++) got |= uart_parser_feed((uint8_t)*c, &evt);

  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL(EVT_TAMPER, evt.type);
  TEST_ASSERT_EQUAL_UINT32(2, evt.data.tamper.tamper_flags);
  TEST_ASSERT_EQUAL_UINT32(700, evt.timestamp_ms);
}

// ── FT-5: Unknown CTRL command is rejected ───────────────────────────────────
void test_unknown_ctrl_rejected(void) {
  uart_parser_init();
  ParsedEvent evt = EVT_EMPTY;
  const char* frame = "$CTRL,CMD=PAUSE*";
  bool got = false;
  for (const char* c = frame; *c; c++) got |= uart_parser_feed((uint8_t)*c, &evt);

  TEST_ASSERT_FALSE(got);
  TEST_ASSERT_EQUAL(EVT_NONE, evt.type);
}

// ── FT-6: VOTE missing CANDIDATE field is rejected ───────────────────────────
void test_vote_missing_candidate_rejected(void) {
  uart_parser_init();
  ParsedEvent evt = EVT_EMPTY;
  const char* frame = "$VOTE,ID=9,TS=123*";
  bool got = false;
  for (const char* c = frame; *c; c++) got |= uart_parser_feed((uint8_t)*c, &evt);

  TEST_ASSERT_FALSE(got);
  TEST_ASSERT_EQUAL(EVT_NONE, evt.type);
}

// ── FT-7: Oversized frame increments parser error count ──────────────────────
void test_oversized_frame_increments_error(void) {
  uart_parser_init();
  ParsedEvent evt = EVT_EMPTY;

  uint32_t before = uart_parser_get_error_count();

  // '$' + 140 body chars + '*' ; body exceeds EVM_FRAME_MAX_SIZE-1 (127)
  TEST_ASSERT_FALSE(uart_parser_feed((uint8_t)'$', &evt));
  for (int i = 0; i < 140; i++) {
    TEST_ASSERT_FALSE(uart_parser_feed((uint8_t)'A', &evt));
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

  e = EVT_EMPTY; e.type = EVT_VOTE;
  e.data.vote.vote_id = 1; e.data.vote.candidate_id = 0; e.timestamp_ms = 100;
  enqueue_and_dispatch(&e);
  e.data.vote.vote_id = 2; e.data.vote.candidate_id = 1; e.timestamp_ms = 110;
  enqueue_and_dispatch(&e);
  e.data.vote.vote_id = 3; e.data.vote.candidate_id = 0; e.timestamp_ms = 120;
  enqueue_and_dispatch(&e);

  e = EVT_EMPTY; e.type = EVT_END;
  enqueue_and_dispatch(&e);
  TEST_ASSERT_EQUAL(STATE_VOTING_CLOSED, supervisor_get_state());

  TEST_ASSERT_EQUAL_UINT32(2, vote_manager_get_count(0));
  TEST_ASSERT_EQUAL_UINT32(1, vote_manager_get_count(1));
  TEST_ASSERT_EQUAL_UINT32(3, vote_manager_get_total());
}

// ── ST-2: EVT_REPORT rejected outside VOTING_CLOSED ──────────────────────────
void test_report_wrong_state(void) {
  ParsedEvent e = EVT_EMPTY;
  e.type = EVT_REPORT;
  TEST_ASSERT_EQUAL(EVM_ERR_WRONG_STATE, supervisor_handle_event(&e));
}

void test_generate_tamper_direct(void);
void test_generate_tamper_via_uart(void);

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
  return UNITY_END();
}

// Generate tamper event directly and verify supervisor enters lockdown state
void test_generate_tamper_direct(void) {
  ParsedEvent e = EVT_EMPTY;
  e.type = EVT_TAMPER;
  e.data.tamper.tamper_flags = TAMPER_CASE_OPEN;   // try TAMPER_VOLTAGE / TAMPER_CLOCK too
  e.timestamp_ms = 1000;

  enqueue_and_dispatch(&e);

  TEST_ASSERT_EQUAL(STATE_TAMPER_DETECTED, supervisor_get_state());
}

// Generate tamper event via UART and verify supervisor enters lockdown state
void test_generate_tamper_via_uart(void) {
  uart_parser_init();
  ParsedEvent evt = EVT_EMPTY;
  const char* frame = "$TAMPER,TYPE=1,TS=700*";   // TYPE=1 => TAMPER_CASE_OPEN
  bool got = false;

  for (const char* c = frame; *c; c++) {
    got |= uart_parser_feed((uint8_t)*c, &evt);
  }

  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL(EVT_TAMPER, evt.type);

  enqueue_and_dispatch(&evt);
  TEST_ASSERT_EQUAL(STATE_TAMPER_DETECTED, supervisor_get_state());
}