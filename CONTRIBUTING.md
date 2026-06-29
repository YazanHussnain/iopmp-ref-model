# Contributing to the RISC-V IOPMP Reference Model

Thanks for your interest in improving this project. This guide describes how to
propose changes, the expectations for pull requests and issues, and our policy on
AI-assisted contributions.

By participating you agree to abide by our [Code of Conduct](CODE_OF_CONDUCT.md).

---

## Ground rules

1. **Spec is the source of truth.** Behavior changes must trace to a section/table/figure
   of the RISC-V IOPMP Architecture Specification (v0.8.2). Cite it in your PR.
2. **Tests are spec-compliant, not model-compliant.** Assertions encode what the spec
   requires. If a test reveals a model deviation, fix the model — do not weaken the test.
   Where the spec genuinely permits a choice, document the chosen option in a comment.
3. **No new build warnings.** The build runs with `-Wall -Wextra -Werror`; keep it clean.
4. **Every change ships with tests.** New behavior gets new assertions in the relevant
   `tests/test_NN_*.c` file (the file that matches its `docs/testplan/` document).
5. **Match the surrounding style.** Follow existing naming, comment density, and idioms.
6. **Keep the public API stable** unless the change is the point; flag API changes loudly.

---

## Development workflow

1. **Fork / branch.** Create a topic branch off `main` (e.g. `fix/mdcfg-improper`,
   `feat/entry-user-cfg`). Do not commit directly to `main`.
2. **Build & test before pushing:**
   ```sh
   cmake -S . -B build
   cmake --build build
   (cd build && ctest --output-on-failure)
   ```
   All suites must pass.
3. **Commit messages.** Use a concise, imperative subject line (≤ ~72 chars), e.g.
   `Fix ERR_REQADDR to store word address per §4.3.3`. Add a body explaining *why* when
   it isn't obvious. Reference issues with `Closes #NN`.
4. **Open a Merge/Pull Request** against `main` using the template below.

---

## Pull Request template

Copy this into your PR description:

```markdown
## Summary
<!-- What does this change do, in one or two sentences? -->

## Spec reference
<!-- Section/table/figure of the IOPMP v0.8.2 spec this relates to. -->

## Type of change
- [ ] Bug fix (model behavior corrected toward the spec)
- [ ] New feature / extension
- [ ] Tests only
- [ ] Docs only
- [ ] Refactor / cleanup (no behavior change)

## Tests
- [ ] Added/updated tests in `tests/test_NN_*.c`
- [ ] `ctest` passes locally with no failures
- [ ] Build is clean under `-Werror`

## AI assistance
- [ ] This change was partially or fully AI-assisted
- [ ] I have reviewed and understand every line I am submitting

## Notes
<!-- Deviations, follow-ups, or anything reviewers should know. -->
```

A PR is ready for review when: it builds clean, all tests pass, it is scoped to one logical
change, and the spec reference is stated.

---

## Issue template

### Bug report
```markdown
**Summary:** <one line>
**Spec reference:** <section/table, if applicable>
**Configuration:** <IopmpParams_t fields / model>
**Steps to reproduce:** <register writes / transaction sequence>
**Expected (per spec):** <what should happen + spec citation>
**Actual:** <what the model does>
**Environment:** <compiler, OS, commit hash>
```

### Feature request
```markdown
**Feature:** <spec feature / extension name and section>
**Motivation:** <why it matters>
**Proposed behavior:** <expected observable behavior + spec citation>
```

---

## Use of AI

AI assistance (code generation, review, test authoring, documentation) is **permitted and
welcome**. It does not replace human judgment:

- **You are accountable** for everything you submit. Review, understand, and verify any
  AI-generated content before opening a PR — the same bar as code you wrote by hand.
- **Disclose it.** Tick the "AI assistance" box in the PR template.
- **Human review is mandatory.** Every PR — AI-assisted or not — is reviewed by a maintainer
  before merge. AI-generated code is never merged unreviewed.
- **Verify against the spec and the tests.** AI output that contradicts the specification or
  fails `ctest` will not be accepted.
- **Respect licensing.** Do not submit content you are not entitled to contribute.

---

## Questions

Open an issue with the `question` label, or start a discussion. We're happy to help you
land your first contribution.
