# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-06-30

Initial release of the RISC-V IOPMP reference model (targets the IOPMP
Architecture Specification, version 0.8.2).

### Added
- **IOPMP model library (`iopmp`)** — register MMIO, RRID→MD (SRCMD) lookup,
  MD→entry-range (MDCFG) lookup, address-mode decode (OFF/TOR/NA4/NAPOT,
  32-bit and 64-bit), priority and non-priority matching, permission checks,
  error capture, and configuration-protection locks.
- **Implementation models** — Full, Rapid-k, Dynamic-k, Isolation, Compact-k.
- **Extensions** — Secondary Permission Setting (SPS), stall / safe runtime
  reconfiguration (MDSTALL/MDSTALLH/RRIDSCP), per-entry interrupt/bus-error
  suppression, Multi-Fault Record (ERR_MFR), MSI interrupt delivery, global
  `no_w`/`no_x`/`xinr`, and RRID translation.
- **Multi-instance router (`iopmp_system`)** — MMIO-base register routing and
  per-instance transaction dispatch for parallel and cascading topologies.
- **Test suite (`tests/`)** — 18 feature-categorized, spec-traced suites
  (`test_01`…`test_18`) covering registers, SRCMD, MDCFG, entries, matching,
  error capture, locks, stall, SPS, non-priority entries, suppression, MFR,
  MSI, HWCFG consistency, appendix-note features, implementation models,
  multi-instance systems, and cross-feature scenarios. Run via CTest.
- **GitLab CI/CD** — build (`-Werror`) and test stages with a JUnit report.
- **Code coverage** — optional gcov instrumentation (`-DIOPMP_COVERAGE=ON`) and
  a CI `gcovr` job producing a Cobertura report (~97% line coverage).
- **Docs** — `README.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, and the
  `docs/testplan/` test plan tracing every behavior to a spec reference.
- **License** — Apache License 2.0.

### Fixed (model brought into spec compliance)
- Partial-hit (`0x04`) is now evaluated before the permission check
  (spec §2.7: reported "irrespective of permission").
- `ERR_REQADDR`/`ERR_REQADDRH` store the word address `addr[33:2]`/`addr[65:34]`
  per spec §4.3.3.
- Stall derivation follows the spec formula
  `rrid_stall[s] = exempt XOR (SRCMD(s).md ∩ selected)` with snapshot semantics.
- SPS register bit layout mirrors `SRCMD_EN` (MD m at bit m+1; high MDs in the
  `*H` registers).
- MD-indexed SRCMD format (`srcmd_fmt=2`) implements the 2-bit-per-RRID
  permission with OR semantics (§A.4.3).
- TOR with `tor_en=0` is coerced to a legal mode (WARL); per-entry suppression
  bits and feature-gated `ERR_CFG`/MSI/MDSTALL fields are wired 0 when their
  capability is absent.

### Removed
- Per-phase build/library split and the associated stub source files.

[Unreleased]: https://gitlab.com/yazanhussnain/iopmp-ref-model/-/compare/v0.1.0...main
[0.1.0]: https://gitlab.com/yazanhussnain/iopmp-ref-model/-/tags/v0.1.0
