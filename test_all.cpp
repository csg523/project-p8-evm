// Compile: g++ -std=c++11 -I. -Istubs test_all.cpp
//   ../src/vote_manager.cpp ../src/storage_manager.cpp
//   ../src/logger.cpp ../src/supervisor.cpp ../src/uart_parser.cpp -o test_evm

#include "stubs/Arduino.h"
#include <cassert>
#include <cstdio>

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
  logger_init();
  storage_reset();
  storage_init();
  vote_manager_init();
  tamper_manager_init();
  uart_parser_init();
  supervisor_init(RESET_COLD_BOOT);

  event_manager_init();
  event_manager_register_handler(EVT_VOTE, route_to_supervisor);
  event_manager_register_handler(EVT_TAMPER, route_to_supervisor);
  event_manager_register_handler(EVT_START, route_to_supervisor);
  event_manager_register_handler(EVT_END, route_to_supervisor);
  event_manager_register_handler(EVT_REPORT, route_to_supervisor);
  event_manager_register_handler(EVT_RESET, route_to_supervisor);
  event_manager_register_handler(EVT_FRAME_ERROR, route_to_supervisor);
}

static void push(ParsedEvent* e) {
  assert(event_manager_enqueue(e));
  event_manager_dispatch_all();
}

// T-1: Duplicate vote rejected
void test_duplicate_vote() {
  reset_all();
  supervisor_request_transition(STATE_VOTING_ACTIVE);
  assert(vote_manager_process(1, 0, 100) == EVM_OK);
  assert(vote_manager_process(1, 0, 110) == EVM_ERR_DUPLICATE);
  printf("T-1 PASS: duplicate vote rejected\n");
}

// T-2: Vote rejected in wrong state
void test_wrong_state_vote() {
  reset_all();  // starts in PRE_ELECTION after cold boot
  ParsedEvent evt = EVT_EMPTY;
  evt.type = EVT_VOTE;
  evt.data.vote.vote_id = 42;
  evt.data.vote.candidate_id = 1;
  evt.timestamp_ms = 200;
  assert(supervisor_handle_event(&evt) == EVM_ERR_WRONG_STATE);
  printf("T-2 PASS: vote rejected in PRE_ELECTION\n");
}

// T-3: Tamper lockdown
void test_tamper_detection() {
  reset_all();
  ParsedEvent evt = EVT_EMPTY;
  evt.type = EVT_TAMPER;
  evt.data.tamper.tamper_flags = TAMPER_CASE_OPEN;
  supervisor_handle_event(&evt);
  assert(supervisor_get_state() == STATE_TAMPER_DETECTED);
  printf("T-3 PASS: tamper lockdown triggered\n");
}

// T-4: No votes accepted after tamper
void test_vote_after_tamper() {
  reset_all();
  supervisor_force_tamper_lockdown(TAMPER_VOLTAGE);
  assert(vote_manager_process(5, 0, 300) ==
         EVM_OK);  // vote_manager itself allows it
  // but supervisor blocks it:
  ParsedEvent evt = EVT_EMPTY;
  evt.type = EVT_VOTE;
  evt.data.vote.vote_id = 6;
  evt.data.vote.candidate_id = 0;
  assert(supervisor_handle_event(&evt) == EVM_ERR_WRONG_STATE);
  printf("T-4 PASS: vote rejected after tamper\n");
}

// FT-1: UART parser rejects frame without $
void test_malformed_frame() {
  uart_parser_init();
  ParsedEvent evt = EVT_EMPTY;
  const char* bad = "VOTE,ID=1,CANDIDATE=0,TS=100*";
  bool got = false;
  for (const char* c = bad; *c; c++) got |= uart_parser_feed((uint8_t)*c, &evt);
  assert(!got);
  printf("FT-1 PASS: malformed frame (no $) rejected\n");
}

// FT-2: Good frame parses correctly
void test_good_frame() {
  uart_parser_init();
  ParsedEvent evt = EVT_EMPTY;
  const char* frame = "$VOTE,ID=7,CANDIDATE=2,TS=500*";
  bool got = false;
  for (const char* c = frame; *c; c++)
    got |= uart_parser_feed((uint8_t)*c, &evt);
  assert(got);
  assert(evt.type == EVT_VOTE);
  assert(evt.data.vote.vote_id == 7);
  assert(evt.data.vote.candidate_id == 2);
  printf("FT-2 PASS: valid VOTE frame parsed\n");
}

// FT-3: CTRL START/END transitions
void test_ctrl_frames() {
  uart_parser_init();
  ParsedEvent evt = EVT_EMPTY;
  const char* start_frame = "$CTRL,CMD=START*";
  bool got = false;
  for (const char* c = start_frame; *c; c++)
    got |= uart_parser_feed((uint8_t)*c, &evt);
  assert(got && evt.type == EVT_START);
  printf("FT-3 PASS: CTRL START frame parsed\n");
}

// ST-1: Full election flow
void test_full_election_flow() {
  reset_all();
  // PRE → VOTING_ACTIVE
  ParsedEvent e = EVT_EMPTY;
  e.type = EVT_START;
  push(&e);
  assert(supervisor_get_state() == STATE_VOTING_ACTIVE);

  // cast 3 votes
  e = EVT_EMPTY;
  e.type = EVT_VOTE;
  e.data.vote.vote_id = 1; e.data.vote.candidate_id = 0; push(&e);
  e.data.vote.vote_id = 2; e.data.vote.candidate_id = 1; push(&e);
  e.data.vote.vote_id = 3; e.data.vote.candidate_id = 0; push(&e);

  // close voting
  e = EVT_EMPTY;
  e.type = EVT_END;
  push(&e);
  assert(supervisor_get_state() == STATE_VOTING_CLOSED);

  assert(vote_manager_get_count(0) == 2);
  assert(vote_manager_get_count(1) == 1);
  assert(vote_manager_get_total() == 3);
  printf("ST-1 PASS: full election flow\n");
}

// int main() {
//   test_duplicate_vote();
//   test_wrong_state_vote();
//   test_tamper_detection();
//   test_vote_after_tamper();
//   test_malformed_frame();
//   test_good_frame();
//   test_ctrl_frames();
//   test_full_election_flow();
//   printf("\nAll tests passed.\n");
//   return 0;
// }