# 17 - Multi-Instance System Routing

**Spec:** §2.2 (Requester/Receiver/Control Port), §A.7 (Run out of MDs - parallel & cascading IOPMP), Figure 1(b); plus the reference model's `libsystem` (`iopmp_system_t`).

The system layer routes MMIO register accesses to the owning instance by `mmio_base`, and routes transactions to an instance by `instance_id`. Each instance has fully independent params/state. Covers parallel deployment (A.7.1) and cascading/gateway (A.7.2).

---

## 17.1 Instance registration & MMIO routing

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SYS-001 | ref-model | Two instances added at mmio_base 0x10000 and 0x20000 | system_read_reg(0x10000+VERSION) | Routes to instance A; returns A's VERSION | |
| IOPMP-SYS-002 | ref-model | Same | system_read_reg(0x20000+VERSION) | Routes to instance B | |
| IOPMP-SYS-003 | ref-model | Write to A's HWCFG region | system_write_reg(0x10000+offset) | Only A's state changes; B unaffected | |
| IOPMP-SYS-004 | ref-model | mmio_addr outside any instance's window | system_read_reg(unmapped) | No match; defined error/0 (no crash) | |
| IOPMP-SYS-005 | ref-model | Overlapping/adjacent windows | Boundary addr resolves to correct instance | Exact base+size containment used | |

## 17.2 Transaction dispatch & independence

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SYS-006 | §A.7.1 | A (Full) allows rrid=1@addr; B (Isolation) denies | system_check(A,1,addr,..) | LEGAL via A; B's rules not consulted | |
| IOPMP-SYS-007 | §A.7.1 | Same txn to instance B | system_check(B,1,addr,..) | Per B's config (independent result) | |
| IOPMP-SYS-008 | ref-model | A error captured (v=1) | Cause violation on B | B's ERR_INFO independent; A's record intact | |
| IOPMP-SYS-009 | ref-model | A locked, B unlocked | Write B config | Succeeds on B; A unaffected | |
| IOPMP-SYS-010 | ref-model | Different params per instance (rrid/entry/md counts) | Init both | Each sized independently; no shared globals | |

## 17.3 Parallel IOPMP (A.7.1)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SYS-011 | §A.7.1 | Address-based routing splits MD space across 2 IOPMPs | Txn to region owned by instance 2 | Routed by address; checked by instance 2 | - |
| IOPMP-SYS-012 | §A.7.1 | RRID-based routing | Txn from RRID in instance 1's set | Routed by RRID | - |
| IOPMP-SYS-013 | §A.7.1 | Aggregate >63 MDs across instances | Use MD beyond single-instance cap | Served by the appropriate parallel instance | - |

## 17.4 Cascading IOPMP / gateway (A.7.2)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SYS-014 | §A.7.2 | Inner IOPMP rrid_transl_en=1, tags new RRID T | Txn passes inner, enters outer | Outer IOPMP receives RRID=T | - |
| IOPMP-SYS-015 | §A.7.2 | Inner denies | Txn | Never reaches outer; inner error reaction | - |
| IOPMP-SYS-016 | §A.7.2 | Outer denies translated RRID | Txn passes inner, outer checks | Outer ILLEGAL per its rules | - |
| IOPMP-SYS-017 | §A.7.2 | Subsystem-level RRID abstraction | Many inner RRIDs -> one outer RRID | Outer manages by subsystem RRID | - |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SYS-X01 | §A.7 + §A.8 | Instance A=Full, B=Compact-k, C=Isolation | Mixed-model system | Each routes & checks per its own model (file 16) | - |
| IOPMP-SYS-X02 | ref-model + §4.3 | Both instances raise interrupts | irq_pending per instance | Independent IRQ lines/callbacks | |
| IOPMP-SYS-X03 | §A.7.2 + §A.7.1 | Cascade feeding a parallel pair | Multi-hop routing | Address/RRID routing composes correctly | - |
| IOPMP-SYS-X04 | ref-model + §5.7 | Stall instance A while B runs | Reconfig A under stall | B transactions unaffected by A's stall | - |
