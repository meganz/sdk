# Memory discipline — auto-memory protocol

Persistent memory lives under
`~/.claude/projects/<repo-slug>/memory/` and is indexed by `MEMORY.md`.
Use it for observations that should survive across sessions; use task
state (TaskCreate, plan-mode) for everything ephemeral.

## Entry types

| Type | Prefix | Purpose | Example |
|---|---|---|---|
| **Feedback** | `feedback_<topic>.md` | User-stated preferences / non-negotiable rules / methodology lessons. | `feedback_tests_sequential.md` |
| **Project** | `project_<id>_<scope>.md` | Investigation results / session outcomes / RCAs scoped to one project. | `project_sdk5360_followup7_16_results.md` |
| **Reference** | `reference_<topic>.md` | Long-lived neutral facts (codebase quirks, env layout). | `reference_test_account_layout.md` |
| **User** | `user_<topic>.md` | Personal preferences not tied to a project (rare). | `user_editor_shortcuts.md` |

In SDK-5360 the active pool is feedback + project entries; reference and
user entries have not been needed.

## Naming convention

- All lower-case, snake_case.
- Project entries: `project_<ticket-id>_<scope>_results.md` for session
  outcomes; `project_<ticket-id>_<topic>.md` for investigations.
- Feedback entries: `feedback_<topic>.md`. Keep the topic short; one
  rule per file.
- HR-specific feedback: `feedback_hr<N>_<topic>.md` (e.g.
  `feedback_hr42_strict_preset.md`).

## When to save

- **Always**: a user-stated rule that affects future sessions ("never
  parallelize integration tests" → `feedback_tests_sequential.md`).
- **Always**: the closing memory entry of every fu7-N session
  (`project_sdk5360_followupN_results.md`).
- **Often**: an investigation outcome that changes the project's
  understanding (e.g. "scopedToPro requires JenkinsCanSpam-SDK
  USERAGENT" → `feedback_scopedtopro_account_requirement.md`).
- **Rarely**: timestamps, exact log paths, ephemeral build dirs — these
  belong in the session's investigation directory.

## When to update

- A previous entry now has a confirmed counter-example → update with a
  dated correction at the top, don't delete.
- A rule has been raised (HR43 → HR46 by fu7-15) → update with the new
  parameter and the date.
- Code has moved → update file:line citations only if the rule still
  applies; otherwise mark the entry stale.

## When to delete

- The rule is permanently obsolete (e.g. preset removed).
- Strict duplicate of another entry. Prefer merge over delete.
- Investigation result superseded by a later one that explicitly cites
  the predecessor.

## MEMORY.md update protocol

Every memory operation (add/update/delete) MUST update `MEMORY.md` in
the same operation:

- New entry → add a one-line index entry with the form
  `` `[short title](filename) — one-line description` ``.
- Updated entry → bump the description if the headline changed; date is
  derived from file mtime, not duplicated in the index.
- Deleted entry → remove the index line.

The user reviews `MEMORY.md` as the at-a-glance state — keep it scannable.

## Ephemeral state vs persistent memory

| Where | What |
|---|---|
| **Plan mode / TodoWrite** | Per-session task list, blockers, time budget. |
| **Investigation directory** | Per-session reports, logs, JSONs, audit files. |
| **`MEMORY.md` + memory files** | Cross-session knowledge, rules, outcomes. |

Do not bloat memory with what belongs in the investigation directory.
A good test: "will the next session benefit from this in a year?" If
yes, memory; if no, investigation dir.

## Staleness handling

Memory entries are point-in-time observations. The harness annotates
entries older than a few days with a system reminder. When you read a
memory entry, treat it as a strong prior but verify any specific
file:line / metric claim against current code.

## Commit hygiene

Memory entries live OUTSIDE the SDK repo (under
`~/.claude/projects/.../memory/`) and are not committed there. If the
team wants memory shared across machines, copy the relevant entries
into the SDK repo as `docs/methodology/<topic>.md` and refactor into
canonical files — that's what this directory is.

## Cross-reference style

Inside a memory entry, reference another entry with bare wiki-style
`[[entry-name]]` or with a markdown link to the other memory file
(both are resolved by the harness). Reference investigation directory
paths with absolute paths (they are stable; the user has the same home
layout).
