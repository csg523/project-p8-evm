
## 1. System Context and Boundaries

### 1.1 System Overview

The EVM controller is responsible for:
- **Accepting and validating** vote inputs from a simulated ballot unit
- **Recording votes immutably** in non-volatile memory
- **Detecting and responding** to physical and logical tampering
- **Managing election lifecycle** through well-defined states
- **Providing audit trails** for verification and accountability
- **Ensuring fail-safe behavior** under abnormal conditions

### 1.2 Inside the System Boundary

The following components and responsibilities are within the EVM controller's domain:

- **Embedded control software** that implements all voting, tamper detection, and state management logic
- **UART input processing** for vote frames, tamper sensor signals, and control commands
- **State machine management** enforcing transitions between election modes
- **Vote storage logic** managing writes to non-volatile memory
- **Tamper detection algorithms** that process sensor inputs and trigger responses
- **Reset and power-loss handlers** that preserve state and ensure consistency
- **Audit report generation** containing vote tallies and tamper event logs

### 1.3 Outside the System Boundary

The following are not the responsibility of the EVM controller:

- **Cryptographic voting protocols** – the system assumes vote data is transmitted securely by external components
- **Voter authentication** – the system assumes authorized personnel validate voter identity
- **Networked communication** – the system operates in isolation; no internet or LAN connectivity is used
- **Graphical user interfaces** – all inputs and outputs use text-based UART frames
- **Election administration procedures** – the system follows commands but does not make policy decisions
- **Physical security** – tamper sensors are external; the system only responds to their signals

### 1.4 Environmental Assumptions and Constraints

The system design assumes:

- **Power can fail at any time** without warning; the system must not lose recorded votes
- **Reset can occur unexpectedly** (watchdog timeout, manual reset); the system must recover consistently
- **UART frames may be delayed, duplicated, missing, or malformed** due to communication issues
- **Sensors may produce noisy or occasional false signals** requiring debouncing and validation
- **Non-volatile memory has finite write endurance** and may degrade over time
- **Election administrators are trustworthy** and issue commands only in appropriate states
- **System clock may experience anomalies** (drift, jumps) requiring detection and response

---

## 2. System Boundary Specification

### 2.1 Functionality Inside the System

| Component | Responsibility |
|-----------|-----------------|
| **Vote Processing** | Accept vote frames from UART, validate candidate ID and election state, record votes exactly once |
| **Election State Management** | Enforce transitions between Initialization, Pre-election, Voting Active, Voting Closed, Tamper-Detected, and Audit modes |
| **Tamper Detection** | Monitor enclosure, voltage, and clock anomalies; trigger immediate safe state transitions when detected |
| **Non-Volatile Storage** | Record votes and tamper events with integrity guarantees; survive power loss without data corruption |
| **Reset and Recovery** | Restore consistent state after unexpected resets; resume from where the system left off without losing votes |
| **Audit and Reporting** | Generate verifiable end-of-election reports with vote counts and tamper event timelines |
| **Fail-Safe Defaults** | Default to non-voting states when system state is uncertain or ambiguous |

### 2.2 External Entities (Outside System Boundary)

| Entity | Role and Interaction |
|--------|---------------------|
| **Simulated Ballot Unit** | Sends vote frames containing candidate ID and timestamp; provides vote confirmation signals via UART |
| **Tamper Sensors** | Physical sensors detect enclosure opens, voltage anomalies, and clock anomalies; signal EVM via UART |
| **Election Administrator** | Issues commands to start election, end election, perform administrative reset; receives audit reports |
| **Non-Volatile Memory Hardware** | Stores vote records and tamper logs; persists across power cycles and resets |
| **Power Supply** | Provides power; can fail or become anomalous at any time |
| **UART Communication Medium** | Connects external inputs to the EVM; may introduce delays, duplicates, or corruption |

### 2.3 System Assumptions

| Assumption | Rationale |
|-----------|-----------|
| Sensors provide independent, asynchronous signals | Allows realistic simulation of hardware interrupts and race conditions |
| UART frames may arrive out of order or duplicated | Realistic model of imperfect serial communication |
| Power loss can occur during vote recording | Requires fail-safe storage design with atomicity guarantees |
| Administrative personnel are trustworthy | Allows focus on software rather than cryptographic authentication |
| Non-volatile memory retains state across resets | Standard assumption for embedded systems with battery-backed RAM or flash |
| Election administrators monitor and respond to tamper alerts | Ensures system can communicate risks to responsible personnel |

---

## 3. Functional Requirements

Functional requirements define what the system must do to fulfill its primary purpose: accurate vote recording and reporting.

### F-1: Vote Recording Integrity

| Aspect | Detail |
|--------|--------|
| **ID** | F-1 |
| **Requirement** | The system shall record each valid vote exactly once in non-volatile memory during the voting active state. |
| **Classification** | Functional (Core) |
| **Priority** | **CRITICAL** |
| **Rationale** | Vote loss or duplication directly undermines election legitimacy and correctness. If violated: votes are lost on power failure, votes are double-counted, or election results are incorrect. |
| **Testability** | Violation is detected by: (1) submitting repeated identical vote inputs and verifying only one is recorded; (2) power-cycling the system during voting and verifying recorded votes are not lost; (3) comparing final vote count to number of valid vote inputs received. |
| **Constraints** | Votes must be written to non-volatile memory before voting handler returns; duplicate votes must be detected and rejected. |

### F-2: Vote Input Validation

| Aspect | Detail |
|--------|--------|
| **ID** | F-2 |
| **Requirement** | The system shall validate all vote inputs before recording, rejecting malformed, invalid, or out-of-state votes. |
| **Classification** | Functional |
| **Priority** | **CRITICAL** |
| **Rationale** | Invalid votes (wrong candidate ID, missing timestamp, duplicate frames) corrupt results and enable vote injection attacks. |
| **Testability** | Violation detected by: (1) submitting vote frames with invalid candidate IDs and verifying rejection; (2) submitting frames with missing required fields; (3) sending votes outside voting active state and verifying rejection. |
| **Constraints** | Validation logic must be centralized and applied before any state change or storage operation. |

### F-3: State-Dependent Vote Acceptance

| Aspect | Detail |
|--------|--------|
| **ID** | F-3 |
| **Requirement** | The system shall accept and record votes only when in the Voting Active state; all other states shall explicitly reject vote inputs. |
| **Classification** | Functional / Safety |
| **Priority** | **CRITICAL** |
| **Rationale** | Votes accepted outside designated voting windows (e.g., during pre-election testing, after closure, during tamper conditions) are fraudulent and invalidate election integrity. |
| **Testability** | Violation detected by: (1) submitting vote during Pre-election state and verifying rejection; (2) submitting vote during Voting Closed state; (3) submitting vote during Tamper-Detected state; (4) verifying final vote count matches only votes received during Voting Active state. |
| **Constraints** | State machine must be explicit and enforced at state transition and vote acceptance points. |

### F-4: End-of-Election Report Generation

| Aspect | Detail |
|--------|--------|
| **ID** | F-4 |
| **Requirement** | The system shall generate a complete end-of-election report containing vote counts per candidate, total votes recorded, timestamp of election closure, and all recorded tamper events when transitioning to Audit/Report mode. |
| **Classification** | Functional |
| **Priority** | **CRITICAL** |
| **Rationale** | Transparency and auditability are essential for election legitimacy. Without a verifiable report, tampering cannot be detected and results cannot be independently verified. |
| **Testability** | Violation detected by: (1) verifying report contains vote count for each candidate; (2) comparing vote count in report to actual votes stored in memory; (3) verifying tamper events are listed with timestamps; (4) verifying report cannot be generated in non-audit states. |
| **Constraints** | Report must be generated from non-volatile storage; must be human-readable; must be consistent with internal vote count. |

### F-5: Election State Machine Implementation

| Aspect | Detail |
|--------|--------|
| **ID** | F-5 |
| **Requirement** | The system shall implement a well-defined state machine with explicit states: Initialization → Pre-election → Voting Active → Voting Closed → Audit/Report, with Tamper-Detected as an orthogonal state reachable from any voting state. |
| **Classification** | Functional |
| **Priority** | **CRITICAL** |
| **Rationale** | Clear state management prevents ambiguous behavior and ensures each operation (vote recording, tamper response) is only allowed in appropriate contexts. |
| **Testability** | Violation detected by: (1) attempting invalid state transitions and verifying rejection; (2) verifying state is persistent across power cycles; (3) verifying all state transitions are logged. |
| **Constraints** | Transitions must be deterministic and explicit; state must be stored in non-volatile memory; invalid transitions must not alter state. |

---

## 4. Safety Requirements

Safety requirements define conditions under which the system must not cause harm or data loss.

### S-1: Tamper Detection and Response

| Aspect | Detail |
|--------|--------|
| **ID** | S-1 |
| **Requirement** | Upon detecting a critical tamper event (enclosure open, voltage anomaly, or clock anomaly), the system shall transition to Tamper-Detected state and disable further voting within a bounded response time of **500 milliseconds**. |
| **Classification** | Safety (Critical) |
| **Priority** | **CRITICAL** |
| **Rationale** | Physical or logical tampering enables vote manipulation, data injection, or memory corruption. A slow response allows attackers time to complete malicious operations before the system responds. |
| **Testability** | Violation detected by: (1) triggering enclosure open sensor and measuring time to state transition; (2) verifying no votes are recorded after tamper event; (3) injecting voltage anomaly and verifying response within 500 ms; (4) verifying tamper event is logged with timestamp. |
| **Constraints** | Response must be interrupt-driven; no voting permitted after transition; all tamper events must be logged; response time must be measured and documented. |

### S-2: Vote Preservation During Power Loss

| Aspect | Detail |
|--------|--------|
| **ID** | S-2 |
| **Requirement** | The system shall ensure all votes recorded prior to power loss are recovered without loss or corruption after power restoration and reset. |
| **Classification** | Safety (Critical) |
| **Priority** | **CRITICAL** |
| **Rationale** | Vote loss on power failure is catastrophic; it makes election results incomplete, unverifiable, and potentially fraudulent. |
| **Testability** | Violation detected by: (1) recording N votes; (2) cutting power unexpectedly; (3) restoring power and rebooting; (4) verifying all N votes are present in non-volatile memory with correct candidate IDs and timestamps. |
| **Constraints** | Must use write-through or atomic write semantics; must not rely on volatile memory for vote data; must validate memory integrity on startup. |

### S-3: Vote Memory Immutability

| Aspect | Detail |
|--------|--------|
| **ID** | S-3 |
| **Requirement** | Once recorded, votes shall not be overwritten, modified, or deleted under any circumstances, including reset, tamper response, or administrative commands. |
| **Classification** | Safety (Critical) |
| **Priority** | **CRITICAL** |
| **Rationale** | Vote modification after recording violates election integrity and enables undetectable fraud. |
| **Testability** | Violation detected by: (1) recording vote with specific candidate ID; (2) attempting various operations (reset, tamper event, admin commands); (3) verifying vote remains unchanged. |
| **Constraints** | No code path shall permit vote deletion or modification; memory layout must separate recorded votes from other data; write protection mechanisms should be implemented in hardware if possible. |

### S-4: Safe State on Uncertainty

| Aspect | Detail |
|--------|--------|
| **ID** | S-4 |
| **Requirement** | When system state is ambiguous, inconsistent, or uncertain (e.g., corrupted state after reset, unrecognized UART frame, memory validation failure), the system shall transition to a safe, non-voting state and halt further vote recording. |
| **Classification** | Safety (Fail-Safe) |
| **Priority** | **CRITICAL** |
| **Rationale** | Ambiguous states can lead to undefined behavior or security vulnerabilities. Conservative fail-safe defaults prevent accidents and attacks. |
| **Testability** | Violation detected by: (1) inducing memory corruption and verifying system halts voting; (2) sending malformed UART frames and verifying system state remains safe; (3) injecting invalid state values and verifying recovery to safe state. |
| **Constraints** | All error paths must lead to explicit safe states; no "default" behaviors that might accidentally permit voting; all state transitions must validate preconditions. |

### S-5: No Automatic Vote Recording

| Aspect | Detail |
|--------|--------|
| **ID** | S-5 |
| **Requirement** | The system shall never automatically record votes without explicit vote input from the ballot unit. The system shall not generate, inject, or assume votes under any conditions. |
| **Classification** | Safety |
| **Priority** | **CRITICAL** |
| **Rationale** | Automatic vote generation enables undetectable fraud. All votes must originate from external, auditable sources. |
| **Testability** | Violation detected by: (1) allowing system to idle in Voting Active state and verifying no votes are recorded; (2) triggering resets, power cycles, or tamper events and verifying no spurious votes appear; (3) examining stored votes and verifying each has a corresponding input frame timestamp. |
| **Constraints** | Vote recording must be purely reactive to external inputs; no internal vote generation logic is permitted. |

### S-6: No Involuntary State Exit from Tamper-Detected

| Aspect | Detail |
|--------|--------|
| **ID** | S-6 |
| **Requirement** | Once entered, the Tamper-Detected state shall be irreversible without explicit administrative authorization and manual intervention. The system shall not automatically recover or transition out of Tamper-Detected state due to sensor readings becoming normal again. |
| **Classification** | Safety |
| **Priority** | **CRITICAL** |
| **Rationale** | Automatic recovery from tamper state would hide evidence of tampering and allow continued fraudulent operations after attack detection. |
| **Testability** | Violation detected by: (1) triggering tamper event and entering Tamper-Detected state; (2) clearing tamper condition (e.g., closing enclosure); (3) verifying system remains in Tamper-Detected state; (4) issuing administrative reset and verifying state returns to Pre-election only after explicit authorization. |
| **Constraints** | Tamper-Detected → Pre-election transition must require explicit admin command; transition must be logged; no automatic recovery pathways exist. |

---

## 5. Non-Functional Requirements

Non-functional requirements define system properties such as performance, reliability, auditability, and usability.

### NF-1: Deterministic Behavior

| Aspect | Detail |
|--------|--------|
| **ID** | NF-1 |
| **Requirement** | The system shall behave deterministically: given the same sequence of inputs, the system shall produce identical sequences of state transitions and vote recordings. |
| **Classification** | Non-Functional (Determinism) |
| **Priority** | **HIGH** |
| **Rationale** | Determinism enables reproducible testing, bug discovery, and forensic analysis of failures. Non-deterministic systems are unverifiable and unreliable in safety-critical contexts. |
| **Testability** | Violation detected by: (1) recording identical input sequences multiple times; (2) comparing outputs (state transitions, vote counts, timestamps); (3) verifying outputs are identical across runs. |
| **Constraints** | No random number generation for vote recording logic; all timestamps must be from system clock; all operations must follow explicit, documented logic. |

### NF-2: Power Loss Robustness

| Aspect | Detail |
|--------|--------|
| **ID** | NF-2 |
| **Requirement** | The system shall recover correctly after sudden power loss at any point during operation, without requiring user intervention or external tools. |
| **Classification** | Non-Functional (Robustness) |
| **Priority** | **CRITICAL** |
| **Rationale** | Embedded systems are subject to power loss and must be designed to tolerate it without corrupting data or requiring manual recovery. |
| **Testability** | Violation detected by: (1) power-cycling system at random intervals during voting; (2) verifying system recovers to a consistent state; (3) verifying recorded votes are not lost or corrupted. |
| **Constraints** | Non-volatile memory must be used for all critical state; volatile memory must not be relied upon for vote data; recovery must be automatic on power restoration. |

### NF-3: Reset Resilience

| Aspect | Detail |
|--------|--------|
| **ID** | NF-3 |
| **Requirement** | The system shall maintain vote integrity and election state consistency across manual resets, watchdog timeouts, and abnormal shutdowns. After reset, the system shall resume operation in a safe, consistent state without user intervention. |
| **Classification** | Non-Functional (Robustness) |
| **Priority** | **CRITICAL** |
| **Rationale** | Resets are common in embedded systems; data corruption after reset is unacceptable in voting systems. |
| **Testability** | Violation detected by: (1) recording votes; (2) triggering reset (manual or watchdog); (3) verifying system restarts in consistent state; (4) verifying all previously recorded votes are present; (5) verifying system resumes from appropriate state. |
| **Constraints** | State machine state must be stored in non-volatile memory; vote count must be recoverable; all critical data must be validated on startup. |

### NF-4: Memory Validation and Corruption Detection

| Aspect | Detail |
|--------|--------|
| **ID** | NF-4 |
| **Requirement** | The system shall detect memory corruption (bit flips, data tampering) in non-volatile vote storage using checksums, CRC, or similar mechanisms and shall refuse to load corrupted data. |
| **Classification** | Non-Functional (Data Integrity) |
| **Priority** | **HIGH** |
| **Rationale** | Non-volatile memory can be corrupted by physical attacks, bit flips, or wear. Silent data corruption is undetectable and catastrophic. |
| **Testability** | Violation detected by: (1) intentionally corrupting vote data in memory; (2) rebooting system; (3) verifying system detects corruption and halts (does not load corrupted votes); (4) verifying system enters safe/audit state for manual recovery. |
| **Constraints** | Every vote record must include a checksum or CRC; system must validate before loading; corrupted records must be logged. |

### NF-5: Response Time for Tamper Events

| Aspect | Detail |
|--------|--------|
| **ID** | NF-5 |
| **Requirement** | The system shall detect and log all tamper events within 50 milliseconds of event occurrence and transition to Tamper-Detected state within 500 milliseconds. |
| **Classification** | Non-Functional (Timing) |
| **Priority** | **CRITICAL** |
| **Rationale** | Rapid response limits the window for attackers to complete malicious operations while the system remains operational. Slower response increases vulnerability window. |
| **Testability** | Violation detected by: (1) timestamp tamper sensor event and system response; (2) measuring elapsed time; (3) verifying both detection and state transition meet timing requirements. |
| **Constraints** | UART polling or interrupt-driven input handling must be fast; state transition logic must be optimized; timing must be validated under load. |

### NF-6: Audit Trail Completeness

| Aspect | Detail |
|--------|--------|
| **ID** | NF-6 |
| **Requirement** | Every security-relevant event (vote recorded, tamper detected, state transition, administrative command, reset) shall be logged with timestamp and event type in non-volatile memory for audit purposes. |
| **Classification** | Non-Functional (Auditability) |
| **Priority** | **HIGH** |
| **Rationale** | Audit trails enable forensic analysis of system behavior, detection of attacks, and verification of correct operation. Incomplete logs hide evidence of tampering. |
| **Testability** | Violation detected by: (1) triggering various system events; (2) examining audit log; (3) verifying all events are recorded with timestamps; (4) verifying log persists across power cycles. |
| **Constraints** | Audit log must use non-volatile memory; log entries must be immutable; log must be readable in Audit/Report mode. |

### NF-7: Bounded Non-Volatile Memory Usage

| Aspect | Detail |
|--------|--------|
| **ID** | NF-7 |
| **Requirement** | The system shall manage non-volatile memory efficiently and shall not require more than **[design team to specify]** kilobytes for vote storage and audit logs in a typical election with up to **1000 votes** and **100 tamper events**. |
| **Classification** | Non-Functional (Resource Efficiency) |
| **Priority** | **MEDIUM** |
| **Rationale** | Embedded systems have limited memory; system must fit within typical embedded flash or EEPROM constraints without requiring external storage. |
| **Testability** | Violation detected by: (1) recording 1000 votes and 100 tamper events; (2) measuring total memory consumed; (3) verifying memory usage is within specified budget. |
| **Constraints** | Design must be space-efficient; vote records should be compact; old audit logs may be pruned after report generation if needed. |

---

## 6. Fault Tolerance and Failure Handling Requirements

The system must gracefully handle faults without losing votes or entering unsafe states.

### FT-1: Malformed UART Frame Handling

| Aspect | Detail |
|--------|--------|
| **ID** | FT-1 |
| **Requirement** | The system shall detect and reject malformed UART frames (missing fields, invalid checksum, truncated frames) without altering system state or crashing. The system shall log rejection with frame details for audit. |
| **Classification** | Fault Tolerance |
| **Priority** | **HIGH** |
| **Rationale** | UART communication is error-prone; malformed frames are common and must not corrupt system state. |
| **Testability** | Violation detected by: (1) sending truncated frames; (2) sending frames with invalid format; (3) verifying system rejects gracefully and continues operation. |
| **Constraints** | Frame parsing must be robust; no assumptions about frame format; all errors logged. |

### FT-2: Duplicate Vote Frame Handling

| Aspect | Detail |
|--------|--------|
| **ID** | FT-2 |
| **Requirement** | The system shall detect and discard duplicate vote frames (same candidate ID, timestamp, and sequence number) without recording the vote twice. |
| **Classification** | Fault Tolerance |
| **Priority** | **CRITICAL** |
| **Rationale** | UART may retransmit frames due to timeout; system must prevent double-counting. |
| **Testability** | Violation detected by: (1) sending identical vote frame twice; (2) verifying vote is recorded only once; (3) verifying duplicate is logged as rejected. |
| **Constraints** | System must track recent vote frame hashes or sequence numbers; must maintain sufficient history for duplicate detection. |

### FT-3: Sensor Noise and Debouncing

| Aspect | Detail |
|--------|--------|
| **ID** | FT-3 |
| **Requirement** | The system shall debounce tamper sensor inputs, requiring sustained anomaly signals for at least [design team to specify: 100-500 ms] before triggering tamper response to avoid false positives from sensor noise. |
| **Classification** | Fault Tolerance |
| **Priority** | **MEDIUM** |
| **Rationale** | Physical sensors are noisy; brief spikes or glitches should not trigger false tamper alarms that halt voting. |
| **Testability** | Violation detected by: (1) sending brief tamper signal (< debounce time); (2) verifying system does not transition to Tamper-Detected; (3) sending sustained signal and verifying transition occurs. |
| **Constraints** | Debounce time must be parameterized and documented; must not defeat legitimate tamper detection; must be tuned for sensor characteristics. |

### FT-4: Watchdog and Internal Failure Detection

| Aspect | Detail |
|--------|--------|
| **ID** | FT-4 |
| **Requirement** | The system shall implement a watchdog timer mechanism to detect internal software failures (infinite loops, stack overflow) and shall automatically reset the system safely, preserving vote integrity. |
| **Classification** | Fault Tolerance |
| **Priority** | **HIGH** |
| **Rationale** | Software bugs can cause the system to hang; watchdog ensures the system recovers without manual intervention. |
| **Testability** | Violation detected by: (1) injecting infinite loop in non-critical code; (2) verifying watchdog triggers reset after timeout; (3) verifying system recovers with votes intact. |
| **Constraints** | Watchdog timeout must be tuned to normal operation; must not interfere with legitimate long operations; reset must be safe (votes preserved). |

---

## 7. Verification and Testing Strategy

### 7.1 Requirements Traceability

Each requirement shall be:
- Traced to test cases
- Validated through unit, integration, or system tests
- Documented with evidence of passing tests

### 7.2 Critical Test Scenarios

| Test Scenario | Purpose | Requirements Covered |
|--------------|---------|----------------------|
| **Nominal voting** | Verify correct vote recording during normal operation | F-1, F-3, S-5 |
| **Power loss during vote** | Verify vote integrity after sudden power loss | S-2, NF-2 |
| **Tamper event response** | Verify system transitions to safe state on tamper | S-1, S-4, NF-5 |
| **Duplicate frame rejection** | Verify system rejects duplicate votes | F-2, FT-2 |
| **Malformed frame handling** | Verify system rejects invalid frames gracefully | FT-1, S-4 |
| **State machine transitions** | Verify all state transitions follow specification | F-5 |
| **Audit report generation** | Verify end-of-election report is complete and correct | F-4, NF-6 |
| **Memory corruption detection** | Verify system detects and rejects corrupted votes | NF-4 |
| **Reset recovery** | Verify system state is consistent after reset | NF-3 |

---

