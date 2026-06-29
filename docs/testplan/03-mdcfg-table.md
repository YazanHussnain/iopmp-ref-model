# 03 - MDCFG Table (Memory Domain -> Entry range)

**Spec:** §2.6 (MDCFG Table), §4.4.1 (MDCFG(m)), §A.5 (Reference behaviors of improper settings), §A.6 (MDCFG reduction - see file 15/16 for k-entry).

MDCFG maps each MD to a contiguous range of entry indices. `MDCFG(m).t` is the upper bound (exclusive); `MDCFG(m−1).t` is the lower bound (inclusive). Entry `j` belongs to MD `m` iff `MDCFG(m−1).t ≤ j < MDCFG(m).t` (for m>0), and `j < MDCFG(0).t` for m=0. This file covers **Format 0** (MDCFG present); formats 1/2 (no MDCFG) are in files 15/16.

---

## 3.1 Range computation

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MDCFG-001 | §2.6 | MDCFG(0).t=2 | Determine entries of MD0 | MD0 owns entries {0,1} | |
| IOPMP-MDCFG-002 | §2.6 | MDCFG(0).t=2, MDCFG(1).t=5 | Entries of MD1 | MD1 owns {2,3,4} | |
| IOPMP-MDCFG-003 | §2.6 | MDCFG(0).t=0 | Entries of MD0 | MD0 owns ∅ (empty domain) | |
| IOPMP-MDCFG-004 | §2.6 | MDCFG(m−1).t == MDCFG(m).t | Entries of MD m | MD m empty (zero entries) | |
| IOPMP-MDCFG-005 | §2.6 | MDCFG(last).t = entry_num | Entries of last MD | Spans up to entry_num−1 | |
| IOPMP-MDCFG-006 | §2.6 | RRID assoc MD1 only; txn addr in entry owned by MD1 | Read txn | Candidate set = MD1's entries only | |
| IOPMP-MDCFG-007 | §2.6 | Entry index j belongs to exactly one MD | Verify partition: build candidates for two RRIDs sharing j | j appears once per MD; an entry belongs to at most one MD | |

## 3.2 MDCFG(m).t register semantics

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MDCFG-008 | §4.4.1 | MDCFG(m).t WARL [15:0] | Write t=7 | Reads back 7 | |
| IOPMP-MDCFG-009 | §4.4.1 | rsv [31:16] = ZERO | Write 0xFFFF_FFFF | t[15:0] legal; [31:16]=0 | |
| IOPMP-MDCFG-010 | §4.4.1 | `md_num`=4 | Read MDCFG(m) m=0..3 | All implemented; MDCFG(4) not implemented | |

## 3.3 Proper / monotonic settings

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MDCFG-011 | §2.6 | enable=1 requires MDCFG(m+1).t ≥ MDCFG(m).t | Program strictly increasing t values | Proper table; all lookups well-defined | |
| IOPMP-MDCFG-012 | §2.6 | MDCFG(m+1).t == MDCFG(m).t (non-decreasing allowed) | Program equal adjacent t | Proper; intermediate MD empty | |

## 3.4 Improper settings (§A.5 / §2.6)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MDCFG-013 | §2.6, §A.5 | MDCFG(m−1).t > MDCFG(m).t (improper) | Lookup MD m | Improper setting; reference behavior per §A.5: one of {corrected, write rejected, MD m has no entries, MDs m..end have no entries} | - |
| IOPMP-MDCFG-014 | §A.5(2) | Implementation rejects improper writes | Write t that would make table improper | Write has no effect; table stays proper | - |
| IOPMP-MDCFG-015 | §A.5(3a) | Implementation leaves improper, isolates MD m | Improper MDCFG(m) | No entries belong to MD m | - |
| IOPMP-MDCFG-016 | §A.5(3b) | Implementation isolates m..md_num−1 | Improper MDCFG(m) | No entries belong to MDs m..md_num−1 | - |
| IOPMP-MDCFG-017 | §2.6 note | Transient improper during programming | Deassociate MDs in SRCMD before reprogramming MDCFG | No security hole during transition (programmer responsibility) | - |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MDCFG-X01 | §2.6 + §2.7 | MD with entries {2,3,4}; entry 3 = TOR | TOR base derives from entry 2 (same MD) | TOR region computed within MD; see file 04 TOR-first-of-MD caveat | |
| IOPMP-MDCFG-X02 | §2.6 + §3.2 | MDCFGLCK.f=2 (MD0,1 locked) | Write MDCFG(1).t | Rejected (locked); MDCFG(2).t writable (file 07) | |
| IOPMP-MDCFG-X03 | §2.6 + §3.2 | MDCFGLCK requires preceding MDs locked when MD m locked | Lock MDCFG(m), check MDCFG(0..m−1) | All preceding must be locked (spec §3.2 note) | |
| IOPMP-MDCFG-X04 | §2.6 + §5.7 | Reprogram MDCFG under stall | Stall affected RRIDs, change MDCFG range, resume | No transaction checked against partial setting (file 08) | - |
| IOPMP-MDCFG-X05 | §2.6 + §A.6 | mdcfg_fmt=1/2 (no MDCFG table) | Compute MD m entries via k | Range = [m*k, m*k+k−1]; MDCFG reads not used (file 16) | - |
