## Use Cases

| Use Case ID | Use Case Name | Primary Actor | Brief Description |
|-------------|---------------|---------------|-------------------|
| UC-1        |Cast Vote      |Voter          |When the election is active, a voter selects a candidate. The system validates the input, records the vote exactly once, and confirms successful recording. If the election is not active or a tamper condition exists, the vote is rejected.                   |
| UC-2        |Detect Tamper Event     |  Tamper Sensor             |   When a tamper sensor signals an abnormal condition, the system detects the event, transitions to a tamper-detected state, disables further voting, and preserves all recorded votes.                |
| UC-3        |Generate Election Report               |Election Official               |After voting is closed, the election official requests the final report. The system generates a summary of recorded votes. If voting is still active, report generation is denied.                   |

---

```mermaid
stateDiagram-v2
    [*] --> POWER_ON
    
    POWER_ON --> INITIALIZATION: System Boot
    
    state INITIALIZATION {
        [*] --> SELF_TEST
        SELF_TEST --> MEMORY_CHECK: HW OK
        MEMORY_CHECK --> STATE_RECOVERY: Memory OK
        STATE_RECOVERY --> READY: Valid State
        
        SELF_TEST --> INIT_FAIL: Test Fail
        MEMORY_CHECK --> INIT_FAIL: Corruption
        STATE_RECOVERY --> INIT_FAIL: Invalid State
    
    }
    state if_state <<choice>>
    INITIALIZATION --> if_state
    if_state --> OPERATIONAL: if all check pass
    if_state --> TAMPER_LOCKDOWN : if fail
    note right of INITIALIZATION
        System boot sequence
        Validate hardware
        Check memory integrity
        Recover previous state
        Fail-safe on any error
    end note
    
    state OPERATIONAL {
        [*] --> PRE_ELECTION
        
        PRE_ELECTION --> VOTING_ACTIVE: Admin Auth + Start
        VOTING_ACTIVE --> VOTING_CLOSED: Admin Auth + End
        VOTING_CLOSED --> AUDIT_REPORT: Admin Auth + Report
        AUDIT_REPORT --> VOTING_CLOSED: Report Complete
        
        note right of PRE_ELECTION
            System ready
            Vote count = 0
            Reject all votes
        end note
        
        note right of VOTING_ACTIVE
            Election in progress
            Record valid votes
            Immutable storage
            Exactly once per voter
        end note
        
        note right of VOTING_CLOSED
            No further votes accepted
            Reject all votes
            Preserve all data
        end note
        
        note right of AUDIT_REPORT
            Display election results
            Vote counts + tampers
            Read-only mode
        end note
    }
    
    state TAMPER_LOCKDOWN {
        [*] --> LOCKDOWN_ACTIVE
        LOCKDOWN_ACTIVE --> LOG_TAMPER: Record Event
        LOG_TAMPER --> DISABLE_VOTING: Preserve Votes
        DISABLE_VOTING --> LOCKDOWN_ACTIVE: Stay Locked
    }
    
     note right of TAMPER_LOCKDOWN
        Types of Tampering:
        someone opened the case
        voltage suddenly drops
        clock tampered
    end note

    note right of TAMPER_LOCKDOWN
        Irreversible lockdown
        All votes preserved
        Requires manual reset
        Terminal fail-safe state
    end note
    
    OPERATIONAL --> TAMPER_LOCKDOWN: Tamper Detected
    TAMPER_LOCKDOWN --> INITIALIZATION: Admin Reset ONLY
```
