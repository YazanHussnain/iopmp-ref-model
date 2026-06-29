# 04 - Entry Array & Address-Mode Decode

**Spec:** §2.4 (IOPMP Entry & Entry Array), §4.6.1 (ENTRY_ADDR / ENTRY_ADDRH), §4.6.2 (ENTRY_CFG r/w/x/a), §4.6.3 (ENTRY_USER_CFG), RISC-V PMP address encoding [1].

Each entry encodes a memory region (`ENTRY_ADDR(i)` = addr[33:2], `ENTRY_ADDRH(i)` = addr[65:34]) and permissions (`r/w/x`) + address mode `a` (OFF/TOR/NA4/NAPOT). This file covers region decoding and per-entry permission bits. Matching/priority is file 05.

---

## 4.1 OFF mode

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ENTRY-001 | §4.6.2 | Entry i a=OFF | Txn to any addr; only entry i could match | Entry i inactive - not a match candidate | |
| IOPMP-ENTRY-002 | §2.4 | Entry index ≥ entry_num | Lookup that entry | Treated as OFF (not available) | |
| IOPMP-ENTRY-003 | §4.6.2 | All entries OFF; RRID associated | Any txn | ILLEGAL etype=0x05 (no rule) | |

## 4.2 NA4 mode

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ENTRY-004 | §2.4, PMP | Entry a=NA4, ENTRY_ADDR=A | Region base | base = A<<2, size = 4 bytes | |
| IOPMP-ENTRY-005 | §2.4 | NA4 region [base, base+4); txn addr=base len=4 | Read, entry r=1 | LEGAL, covers all bytes | |
| IOPMP-ENTRY-006 | §2.4 | NA4 region; txn addr=base len=8 (spans beyond 4) | Read | Partial hit ⇒ etype=0x04 (file 05) | |
| IOPMP-ENTRY-007 | §2.4 | NA4; txn addr=base+4 (just outside) | Read | No match by this entry | |

## 4.3 NAPOT mode

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ENTRY-008 | §2.4, PMP | NAPOT, ENTRY_ADDR encodes 16-byte region | Decode base/size | base = (A & ~tz)<<2; size = (trailing_ones+1)<<2 = 16 | |
| IOPMP-ENTRY-009 | §2.4 | NAPOT 4KB region; txn fully inside | Read r=1 | LEGAL | |
| IOPMP-ENTRY-010 | §2.4 | NAPOT region; txn crosses upper boundary | Read | Partial hit etype=0x04 | |
| IOPMP-ENTRY-011 | §2.4 | NAPOT smallest (8 bytes, addr=...0b1) | Decode | size=8; base aligned | |
| IOPMP-ENTRY-012 | §2.4 | NAPOT all-ones low bits (very large region) | Decode | Large power-of-2 region; no overflow | |

## 4.4 TOR mode

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ENTRY-013 | §2.4, §4.1.3 | `tor_en`=1; entry i a=TOR, entry i−1.addr=L, entry i.addr=H | Decode region | [L<<2, H<<2); size=(H−L)<<2 | |
| IOPMP-ENTRY-014 | §2.4 | TOR at index 0 (no previous entry) | Decode | Lower bound = 0; region [0, addr<<2) | |
| IOPMP-ENTRY-015 | §2.4 note | TOR is **first entry of a MD** (prev entry in a different MD) | Decode region | Region derives from previous MD's entry - flagged hazard; programmer should avoid TOR as first entry of MD | |
| IOPMP-ENTRY-016 | §2.4 note | Previous entry changed unexpectedly | TOR region after prev change | Region shifts with prev.addr - documents the cross-MD aliasing risk | |
| IOPMP-ENTRY-017 | §2.4 | TOR where H ≤ L (empty/negative range) | Decode | Empty region; never covers any byte | |
| IOPMP-ENTRY-018 | §4.6.2 | `tor_en`=0; write a=TOR | WARL behavior | TOR not retained (illegal mode), reads a legal value | |

## 4.5 64-bit addresses (ENTRY_ADDRH)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ENTRY-019 | §4.6.1, §4.1.3 | `addrh_en`=1; ENTRY_ADDRH(i) sets addr[65:34] | NAPOT region above 34-bit boundary | base uses full 66-bit address; txn at high addr matches | - |
| IOPMP-ENTRY-020 | §4.6.1 | `addrh_en`=0 | Read ENTRY_ADDRH(i) | Not implemented; reads 0 | |
| IOPMP-ENTRY-021 | §4.6.1 | `addrh_en`=1; some MSBs hardwired (single segment) | Write ENTRY_ADDRH MSBs | Hardwired bits read fixed value (WARL) | - |
| IOPMP-ENTRY-022 | §4.6.1 | 64-bit TOR spanning the 34-bit boundary | Decode | Region computed across boundary correctly | - |

## 4.6 Permission bits (r/w/x) WARL

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ENTRY-023 | §4.6.2 | Entry r=1,w=0,x=0 | Read / Write / Exec txn fully inside | Read LEGAL; Write etype=0x02; Exec etype=0x03 | |
| IOPMP-ENTRY-024 | §4.6.2 | Entry r=0,w=1 | Read txn | etype=0x01 (illegal read) | |
| IOPMP-ENTRY-025 | §4.6.2 | Entry r=0,w=0,x=1 | Exec txn | LEGAL (x granted) | |
| IOPMP-ENTRY-026 | §4.6.2 | All r/w/x=0 (entry active, no perms) | Any access | Illegal per access type (0x01/0x02/0x03) | |
| IOPMP-ENTRY-027 | §4.6.2 | Implementation constrains combos (e.g. w⇒r) | Write w=1,r=0 | WARL retains only legal combo | - |
| IOPMP-ENTRY-028 | §4.6.3 | ENTRY_USER_CFG implemented | Program user attr; issue txn | IMP-defined extra rule applied | - |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ENTRY-X01 | §2.4 + §2.7 | TOR entry i and i−1 in same MD, both candidates | Txn in TOR region | TOR region honored; priority by index (file 05) | |
| IOPMP-ENTRY-X02 | §2.4 + §3.3 | ENTRYLCK.f locks entry i | Write ENTRY_ADDR(i)/CFG(i) | Rejected; region unchanged (file 07) | |
| IOPMP-ENTRY-X03 | §4.6.1 + §4.3.3 | addrh_en=1, violation at high addr | Capture error | ERR_REQADDRH populated with addr[65:34] (file 06) | - |
| IOPMP-ENTRY-X04 | §4.6.2 + §A.2 | no_x=1 (global fetch deny) | Exec txn even if entry x=1 | ILLEGAL etype=0x05 (global deny overrides) (file 15) | - |
| IOPMP-ENTRY-X05 | §2.4 + §5.3 | Entry is non-priority | Txn partially covering region | No 0x04; must cover ALL bytes to match (file 10) | - |
