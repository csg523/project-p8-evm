#pragma once
#include "evm_events.h"
#include "evm_types.h"

void uart_parser_init(void);

// Returns true and fills *evt when a complete, valid frame is decoded.
bool uart_parser_feed(uint8_t byte, ParsedEvent* evt);

void uart_parser_reset(void);
uint32_t uart_parser_get_error_count(void);
const ParserState* uart_parser_get_state(void);