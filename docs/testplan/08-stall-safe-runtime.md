# 08 - Stall Mechanism & Safe Runtime Configuration

**Spec:** §5.7 (Safe Runtime Configuration), §5.7.1 (atomicity), §5.7.2 (programming steps), §5.7.3 (stall transactions), §5.7.4 (cherry pick / RRIDSCP), §5.7.5 (faulting stalled), §5.7.6 (resume), §5.7.7 (order to stall), §5.7.8 (implementation options), §5.1.2 (MDSTALL/MDSTALLH), §5.1.3 (RRIDSCP), §5.1.4 (ERR_CFG.stall_violation_en).

The stall extension lets software update IOPMP rules atomically: stall affected RRIDs -> update -> resume. `rrid_stall[s]` is derived from MDSTALL at the moment of writing: `rrid_stall[s] ⇐ MDSTALL.exempt ^ (Reduction_OR(SRCMD(s).md & stall_by_md))`. All registers here are optional (HWCFG2.stall_en).

---

## 8.1 Capability & register presence

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-STALL-001 | §5.1.1 | HWCFG2.stall_en=1 | Read MDSTALL/MDSTALLH/RRIDSCP | Implemented; writable | - |
| IOPMP-STALL-002 | §5.7.8 | stall_en=0 | Read MDSTALL/MDSTALLH/RRIDSCP/ERR_CFG.stall_violation_en | All return 0 | - |
| IOPMP-STALL-003 | §5.7.8 | `md_num`<32 | Read MDSTALLH | Wired 0 | - |
| IOPMP-STALL-004 | §5.7.8 | Partial impl: some MDSTALL.md bits unimplemented | Write all-1s to MDSTALL.md, read back | Only implemented bits return 1 | - |

## 8.2 MDSTALL semantics & rrid_stall derivation

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-STALL-005 | §5.7.3 | exempt=0; MDSTALL.md selects MD m; RRID s associated with MD m | Write MDSTALL | rrid_stall[s]=1 (associated RRIDs stalled) | - |
| IOPMP-STALL-006 | §5.7.3 | exempt=1; same selection | Write MDSTALL | rrid_stall[s]=1 for RRIDs **not** associated with any selected MD; associated ones run | - |
| IOPMP-STALL-007 | §5.7.3 | exempt=0, no MD selected (md=0) | Write MDSTALL | rrid_stall all 0 (nothing stalled) | - |
| IOPMP-STALL-008 | §5.7.3 | exempt=1, md=0 | Write MDSTALL | All RRIDs stalled (exempt of empty set) | - |
| IOPMP-STALL-009 | §5.1.2 | After writing MDSTALL | Read MDSTALL.md | Returns selected MD bitmap | - |
| IOPMP-STALL-010 | §5.7.3 note | rrid_stall snapshots SRCMD at write time | Change SRCMD after MDSTALL write | rrid_stall unchanged (snapshot semantics) | - |
| IOPMP-STALL-011 | §5.1.2 | MDSTALLH for MD 31+ | Write MDSTALLH.mdh selecting high MD | High MDs selected; combined with MDSTALL.md | - |

## 8.3 is_busy handshake

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-STALL-012 | §5.1.2, §5.7.7 | Write MDSTALL | Read MDSTALL.is_busy | 0 = took effect; 1 = not yet (poll until 0) | - |
| IOPMP-STALL-013 | §5.7.6 | Resume (write 0 to MDSTALLH then MDSTALL) | Poll is_busy | Clears to 0 within finite time; all resumed | - |
| IOPMP-STALL-014 | §5.7.8 | is_busy wired 0 (no race) impl | Read is_busy | Always 0 | - |

## 8.4 Stall effect on transactions

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-STALL-015 | §5.7.3 | rrid_stall[s]=1 | Txn from RRID s | Transaction stalled (held; not checked) - caller retries | - |
| IOPMP-STALL-016 | §5.7.3 | rrid_stall[s]=0 | Txn from RRID s during others' stall | Checked normally (not stalled) | - |
| IOPMP-STALL-017 | §5.7.6 | Resume all | Previously stalled txn retried | Now checked against updated rules | - |

## 8.5 RRIDSCP - cherry pick

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-STALL-018 | §5.1.3, §5.7.4 | After MDSTALL; RRIDSCP.rrid=s, op=1 (stall) | Write RRIDSCP | rrid_stall[s]=1 (selected) | - |
| IOPMP-STALL-019 | §5.7.4 | RRIDSCP.rrid=s, op=2 (don't stall) | Write RRIDSCP | rrid_stall[s]=0 (deselected) | - |
| IOPMP-STALL-020 | §5.1.3 | RRIDSCP.op=0 (query), rrid=s | Write then read RRIDSCP.stat | stat reflects: 1=stalled, 2=not stalled, 3=unimplemented/unselectable | - |
| IOPMP-STALL-021 | §5.1.3 | RRIDSCP not implemented | Write rrid, read stat | stat=0 (RRIDSCP not implemented) | - |
| IOPMP-STALL-022 | §5.7.4 | op=3 | Write RRIDSCP.op=3 | Reserved; no effect | - |
| IOPMP-STALL-023 | §5.7.8 | Unselectable RRID written to RRIDSCP.rrid | Read stat | stat=3 | - |

## 8.6 Faulting stalled transactions (etype 0x07)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-STALL-024 | §5.7.5, §5.1.4 | ERR_CFG.stall_violation_en=1; rrid_stall[s]=1 | Txn from RRID s that cannot be buffered | Faulted: ILLEGAL etype=0x07; logged in ERR_INFO | - |
| IOPMP-STALL-025 | §5.7.5 | stall_violation_en=0 | Stalled txn | Held (stalled), never faulted; etype 0x07 must not occur | - |
| IOPMP-STALL-026 | §5.1.4 | stall_violation_en WARL | Write 1 / 0 | Programmable or fixed per impl | - |

## 8.7 Programming order (§5.7.7)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-STALL-027 | §5.7.7 | Full sequence | Step1.1 write MDSTALLH; 1.2 write MDSTALL once; 1.3 write RRIDSCP; 1.4 poll is_busy==0; Step2 update rules; Step3.1 MDSTALLH=0; 3.2 MDSTALL=0; 3.3 poll is_busy==0 | Atomic update; no txn checked against partial setting | - |
| IOPMP-STALL-028 | §5.7.7 | Write nonzero MDSTALL twice without resume | Second write | rrid_stall undefined (spec: at most once before resume) | - |
| IOPMP-STALL-029 | §5.7.6 | Resume: write 0 to MDSTALL | After write | All transactions resumed; rrid_stall cleared | - |
| IOPMP-STALL-030 | §5.7.7 | Writing MDSTALLH only holds value | Write MDSTALLH (no MDSTALL) | rrid_stall NOT updated (only MDSTALL.exempt write triggers update) | - |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-STALL-X01 | §5.7 + §2.6 | Stall RRIDs of MD m, reprogram MDCFG(m) range, resume | End-to-end safe reconfig | No partial-setting check; post-resume uses new range | - |
| IOPMP-STALL-X02 | §5.7 + §3 | Stall affects only unlocked MDs; locked MD entries unchanged | Reconfig under stall | Locked config preserved (file 07) | - |
| IOPMP-STALL-X03 | §5.7.5 + §4.3 | stall_violation_en=1; faulted txn while v=0 | Capture | etype=0x07 in ERR_INFO; ie⇒interrupt (file 06) | - |
| IOPMP-STALL-X04 | §5.7.4 + §2.5 | MDSTALL grouping insufficient; use RRIDSCP to fine-tune | Mixed MDSTALL + RRIDSCP | Final rrid_stall = MDSTALL result then RRIDSCP overrides per RRID | - |
| IOPMP-STALL-X05 | §5.7.8 + §A.8 | Isolation model (RRID i->MD i) under stall | MDSTALL.md selects MD m | Only RRID m stalled (1:1 mapping) (file 16) | - |
