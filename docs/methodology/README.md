# MEGA SDK — Operational methodology

## Purpose

`docs/methodology/` holds the operational session methodology used to
land long-running performance, concurrency, and refactoring work on the
MEGA SDK. It documents **how** a multi-session ticket is run end to
end: the FollowupRequest → execution → Verdict loop, the
regression-sweep cadence between commits, the hard-rules catalogue, and
the per-session memory protocol. It is a per-contributor and per-ticket
artifact — different maintainers and different tickets each maintain
their own live versions.

## How this differs from `CLAUDE.md` and `AGENTS.md`

- **`CLAUDE.md`** and **`AGENTS.md`** at the repo root document how
  an AI agent should behave when working on this codebase — build
  commands, code style, where logs go, test-running etiquette. Each
  contributor keeps their own copy locally; both files are gitignored.
  Scope is **agent behaviour on this codebase**.
- **`docs/methodology/`** documents how to structure a multi-session
  ticket: the loop, the rules, the regression-sweep cadence, the
  vocabulary. Most of it is per-contributor and per-ticket too;
  see "What's tracked vs not" below. Scope is **ticket structure
  and procedure**.

Both are agent-friendly artifacts. `CLAUDE.md` / `AGENTS.md` cover the
**codebase**; `docs/methodology/` covers the **ticket workflow**.

## What's tracked vs gitignored in this directory

**Tracked (committed to the SDK repo):**

| Path | What it is |
|---|---|
| `README.md` (this file) | Explanation of the directory. |
| `portable_template/` | Starter scaffold for adopting the methodology in a new repo or new ticket. Self-contained; safe to copy out. |
| `benchmark_discipline.md` | SDK-universal bench framework usage guide (build, JSON artifact schema, throttle-storm caveats, comparison recipes). |
| `tsan_discipline.md` | SDK-universal TSAN usage guide (presets, TSAN_OPTIONS, WS-upload surface filter, race classification). |

**Gitignored (per-contributor / per-ticket):**

| Path (live, on-disk only) | What it is |
|---|---|
| `followup_workflow.md` | The FollowupRequest → plan → execute → Verdict → memory → push loop and per-Goal pattern. |
| `fu7_N_session_template.md` | Copy-paste FollowupRequest skeleton for the next session in a series. |
| `glossary.md` | Live session-specific terminology (cell short names, session IDs, hard-rule IDs, internal labels). |
| `hard_rules_catalog.md` | Live catalogue of hard rules with origin tags. |
| `memory_discipline.md` | Per-contributor auto-memory protocol (Claude harness memory layout, naming, lifecycle). |
| `regression_sweep_cadence.md` | Live regression-sweep tier scope and cadence rules. |
| Any other contributor-added session files | Per-session notes, draft hard rules, draft Verdicts. |

The gitignored files exist on a contributor's local disk and travel
with their working tree, but they don't land in the public SDK history.

## Recommended usage

**For a new ticket adopting this methodology**

1. Copy `portable_template/` into your local working directory **outside
   the SDK tree** (e.g. into a separate methodology repo you own).
2. Instantiate the placeholders for your ticket (rename
   `CLAUDE.template.md` → `CLAUDE.md`, fill in the
   `hard_rules_catalog.template.md` with your initial rules, etc.).
3. Run your sessions following the loop in the portable template.
4. Land code changes in the SDK; archive the live methodology
   artifacts (FollowupRequest / Verdict pairs, hard-rules log, memory
   entries) to your own private repo.

**For an existing ticket**

- Keep your live `followup_workflow.md`, `fu7_N_session_template.md`,
  `hard_rules_catalog.md`, etc. in `docs/methodology/` locally —
  they are gitignored, so they stay on your machine.
- Push only the code changes to the SDK; keep the methodology archive
  in your private repo.

## Examples

### Example 1 — SDK-5360 (the originating ticket)

The ticket that produced this methodology ran for ~20 multi-session
"followup7-N" cycles over several months. The live archive includes:

- Hard rules HR1..HR52, each with an origin tag.
- A regression-sweep cadence that mapped each commit to the tier of
  tests it had to satisfy before the next commit could land.
- A FollowupRequest + Verdict pair for every session.
- ~150 auto-memory entries capturing intermediate findings.

All session artifacts live in the contributor's private methodology
repository (which mirrors the gitignored portion of this directory).
The public SDK tree retains only the universal pieces: the bench
framework, the TSAN surface-filter list, and the `portable_template/`
starter scaffold.

### Example 2 — a future ticket (e.g. SDK-XXXX adopting this methodology)

1. The contributor copies `portable_template/` into their local
   workspace, **not** into the SDK tree.
2. They draft their own `FollowupRequest.md` per the template's loop.
3. They add hard rules as discovered (HR1, HR2, ... each with an
   origin marker).
4. They run sessions; each session's Verdict goes into their private
   repo.
5. They land code changes in the SDK; the methodology archive stays
   private.

## Pointers

- [portable_template/README.md](portable_template/README.md) — adoption
  scaffold for a new repo or new ticket.
- [benchmark_discipline.md](benchmark_discipline.md) — SDK bench
  framework usage (universal).
- [tsan_discipline.md](tsan_discipline.md) — SDK TSAN usage (universal).
- [docs/ci/CI_TESTING_GUIDE.md](../ci/CI_TESTING_GUIDE.md) — Jenkins
  trigger phrases, preset glossary, artifact patterns.
- [tests/integration/BENCHMARKS.md](../../tests/integration/BENCHMARKS.md)
  — benchmark cell catalogue.
- `CLAUDE.md` / `AGENTS.md` at the repo root (gitignored) —
  per-contributor agent guidance for this codebase.
