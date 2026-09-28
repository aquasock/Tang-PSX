# core-log.md Entry Format

Normative formatting rules for `.ai/core-log.md`. Read this document before
writing or changing an entry. `.ai/core.md` remains authoritative for project
policy; this document governs the scope, timing, and shape of log entries.

The active log is a concise engineering handoff, not a live plan or full test
transcript. An entry records the completed result of one approved workflow
cycle after the cycle reaches a terminal result. Proposals and work in progress
remain in the active conversation until implementation, validation, or a
terminal failure produces a result worth committing.

---

## Canonical Template

Copy this template without adding sections.

```markdown
## 1 COMMIT Unreleased 2026-09-27T12:34:56-07:00

#### Coming From:

Unreleased d923535

#### Purpose:

(One-sentence statement of the approved cycle's purpose.)

#### Outcome:

(One standard English paragraph recording what changed, the relevant build and deployment evidence, and the user's test result.)

#### Next Steps:

(One standard English paragraph stating the next actionable work or that no work remains.)

#### Files Modified:

- path/to/source_file.sv

#### Status:

- Build: PASS
- Deployment: PASS
- User Test: PASS

---
```

An entry has exactly the six `####` sections shown above, in that order, and
ends with `---` on its own line.

---

## When to Write an Entry

Write one entry after an approved workflow cycle reaches one of these terminal
points:

- The build, deployment, and user test completed.
- The build failed and prevented deployment.
- Deployment failed and prevented user testing.
- The user rejected, stopped, or deferred the cycle after implementation began.
- A documentation-only or diagnostic-only cycle completed without a hardware
  build being applicable.

Do not write a proposal entry. Do not use a placeholder entry while work is in
progress. In particular, the old `???` hash workflow is prohibited.

Update the log after gathering the cycle's final evidence and before committing
and pushing the completed cycle. The entry must be part of the same commit as
the changes it describes. Git history identifies that resulting commit, so the
entry must not attempt to include its own commit hash.

Use one entry per workflow cycle and one workflow cycle per commit. If a new
finding materially changes an approved plan, stop for user approval as required
by `.ai/core.md`; continue the existing cycle only if the revised plan is
approved and remains a coherent commit boundary.

---

## Header Line

`## <number> <type> <version> <timestamp>`

| Field | Rule |
|---|---|
| `<number>` | Sequential entry number in the active log, beginning at `1`; no zero padding. |
| `<type>` | `COMMIT` or `VERSION`. These are the only record types. |
| `<version>` | `Unreleased` for ordinary development, or the leading-`v` semantic version tag for a release boundary. |
| `<timestamp>` | ISO-8601 timestamp with the local UTC offset for America/Phoenix, including seconds. |

Use one space between fields. Do not add punctuation, parentheses, a commit
hash, or trailing text.

### Record Types

`COMMIT` records an ordinary completed workflow cycle that will be committed
and pushed after the log entry is written.

`VERSION` records a release boundary governed by the Versioning and Releasing
sections of `.ai/core.md`. Its version field must be the release tag, such as
`v0.1.0`.

Do not invent proposal, test, diagnostic, failure, handoff, or documentation
record types. Those results are all `COMMIT` entries and are distinguished by
their content and Status values.

---

## The Six Sections

All six sections are mandatory and must appear in the canonical order. Leave
one blank line after every `####` heading. If a section has no applicable
content, write `None.` rather than omitting it.

### 1. Coming From:

Record the repository state on which the approved cycle began:

`<version> <short-hash>`

Use `Unreleased` or the applicable leading-`v` version followed by the
abbreviated commit SHA of `HEAD` before the cycle's changes. This is a base
reference, not the hash of the future commit containing the entry.

### 2. Purpose:

Write exactly one sentence stating why the approved cycle was performed. Do not
describe results here.

### 3. Outcome:

Write one standard English paragraph describing what was actually implemented
and what happened. Keep only the evidence a future agent needs to recover the
project state.

When applicable, include:

- The relevant build result and build configuration.
- The identity or digest of the artifact deployed to the Tang.
- The deployment result and important target state.
- The user's reported hardware result.
- Any rejected hypothesis, regression, or limitation that affects the next
  cycle.
- The reason later workflow stages were not run after a failure.

The entry itself must remain prose: do not use lists, tables, code blocks, or
additional headings inside Outcome. Inline code formatting is allowed for
paths, identifiers, versions, and hashes.

Detailed command output, exhaustive measurements, and disposable diagnostic
data do not belong in the active log. Put durable evidence in an appropriate
repository file or deterministic tool when it is necessary to reproduce the
result, and cite that path in the prose.

### 4. Next Steps:

Write one standard English paragraph stating the next actionable work, the
validation still needed, or that no work remains. Do not use lists, tables,
code blocks, or additional headings.

Next Steps must reflect the state after the recorded cycle. It must not preserve
a superseded plan or instruct a future agent to repeat validation already
completed.

### 5. Files Modified:

List repository-relative paths changed by the engineering work, one path per
line with a `- ` prefix. Include source, constraint, build-system,
configuration, and deterministic tool files when applicable.

Do not list:

- Files under `.ai/`.
- Generated build products or binary artifacts.
- Files inspected but not changed.
- External files outside this repository.

Write `None.` when the cycle changed only project-control metadata or produced
no committed engineering-file change.

### 6. Status:

Use exactly these three lines and replace only the value:

```markdown
- Build: PASS
- Deployment: PASS
- User Test: PASS
```

Allowed values are:

| Value | Meaning |
|---|---|
| `PASS` | The stage completed successfully. |
| `FAIL` | The stage ran and failed or was rejected. |
| `NOT RUN` | The stage was applicable but did not run. |
| `N/A` | The stage did not apply to this cycle. |

Use `User Test: PASS` only when the user reports acceptance of the hardware
result. An agent-run simulation, lint check, checksum comparison, or
device-side diagnostic is not a substitute for user acceptance.

For a normal hardware cycle, all three stages are applicable. If Build fails,
Deployment and User Test normally become `NOT RUN`. If Deployment fails, User
Test normally becomes `NOT RUN`. For a project-control-only change, all three
values may be `N/A`; describe the document review or audit in Outcome.

Do not add annotations, hashes, slack figures, or explanations to the Status
lines. Put explanations in Outcome.

---

## Entry Immutability and Corrections

After the entry is committed, treat it as settled history. Do not append later
results or rewrite the recorded outcome. Record new information in the next
entry and name the superseded entry in Outcome.

Before commit, correct the pending entry so it accurately describes the final
contents and evidence of that cycle. After commit, only a mechanical correction
needed to restore conformance may edit the entry, and the correcting commit must
contain a new entry explaining the correction.

---

## Active-Log Capacity and Archival

The active `.ai/core-log.md` is limited to 100 entries by `.ai/core.md`. Count
top-level entry headings, not line count or the highest historical number.

When 100 entries are present and a new entry is required:

1. Create a timestamped `tar.gz` archive of the completed active log under
   `archived_logs` as required by `.ai/core.md`.
2. Clear the active log.
3. Add a handoff as entry `1` using the normal `COMMIT` format, identifying the
   archived log by filename and summarizing the live project state.
4. Use `N/A` for stages that do not apply to the archival handoff.

Continue numbering sequentially from the handoff entry. Do not inspect or cite
older archives without user approval. Archiving the active log during required
rollover is maintenance, not permission to consult other archived logs.

---

## Prohibited

1. Writing a proposal or work-in-progress entry.
2. Using `???` or another future-commit placeholder.
3. Putting the resulting commit hash in its own entry.
4. Adding a seventh `####` section or changing the section order.
5. Inventing record types or Status labels.
6. Using a Status value other than `PASS`, `FAIL`, `NOT RUN`, or `N/A`.
7. Using tables, lists, code blocks, or extra headings inside Purpose, Outcome,
   or Next Steps.
8. Omitting a mandatory section or leaving its body empty.
9. Listing `.ai/` files, generated artifacts, or unchanged files under Files
   Modified.
10. Rewriting settled history instead of recording a correction in a new entry.
11. Allowing the active log to exceed 100 entries.

---

## Core-Syntax Audit

Any change to a file in `.ai/`, other than a change only to
`.ai/core-syntax.md`, requires a syntax audit before commit as directed by
`.ai/core.md`.

The audit must:

1. Re-read `.ai/core.md` and this document.
2. Inspect the complete `.ai/` diff.
3. Confirm that `.ai/core.md` was not changed without an explicit user request.
4. Validate every added or modified log entry against the template, ordering,
   prose, Status, numbering, and 100-entry rules.
5. Confirm that project-control changes agree with the current workflow and do
   not rewrite settled history.
6. Correct all conformance failures before commit.

Record the audit result in the current entry's Outcome when the workflow cycle
includes a log entry. For a `.ai/core-syntax.md`-only change, no audit is
required by policy, though a direct consistency review against `.ai/core.md` is
recommended.

---

## Pre-Commit Checklist

- [ ] The entry was written after the cycle reached a terminal result.
- [ ] The header has four fields in the required order.
- [ ] The entry number is sequential in the active log.
- [ ] The type is `COMMIT` or `VERSION`.
- [ ] The timestamp is ISO-8601 with the America/Phoenix UTC offset.
- [ ] Coming From identifies the pre-cycle version and abbreviated `HEAD` SHA.
- [ ] Exactly six `####` sections appear in canonical order.
- [ ] Purpose is exactly one sentence.
- [ ] Outcome and Next Steps are prose without nested lists, tables, code blocks,
      or headings.
- [ ] Files Modified contains only eligible repository-relative paths, or
      `None.`.
- [ ] Status contains exactly three required lines with allowed values.
- [ ] The entry ends with `---`.
- [ ] The active log contains no more than 100 entries.
- [ ] The entry and described changes will be committed together.
- [ ] The required core-syntax audit passed.

---

## Current Log Conformance

Existing settled entries are historical evidence and are not reformatted merely
because this syntax changes. The active Tang-Phosphor log is currently empty, so
its first workflow entry begins at `1` and must conform to this document.
