# MEGA SDK — Operational methodology

This directory captures the operational methodology developed during
SDK-5360 (WS uploads). Use it as the starting point for any session that
needs to maintain the same discipline, whether you are a new contributor
landing on the codebase or an agent-led follow-up session.

## When to use this

- **New contributors** — read in order: `glossary.md` →
  `followup_workflow.md` → `regression_sweep_cadence.md` →
  `hard_rules_catalog.md`.
- **New fu7-N+ sessions** (or any "followupN" continuation on a long-lived
  branch) — start from `fu7_N_session_template.md`; cite the catalog
  entries you need; consult `memory_discipline.md` before recording new
  observations.
- **Adoption in another repo** — copy `portable_template/` into the
  target repo's `docs/methodology/` and follow `portable_template/README.md`.

## Files in this directory

| File | Purpose |
|---|---|
| [followup_workflow.md](followup_workflow.md) | The `FollowupRequest → plan → execute → Verdict → memory → push` loop and per-Goal pattern. |
| [memory_discipline.md](memory_discipline.md) | Auto-memory protocol: types, naming, when to save/update/delete, ephemeral vs persistent. |
| [regression_sweep_cadence.md](regression_sweep_cadence.md) | HR14/33/34/40/43/46/47 — what runs at every commit, between Goals, and at the final sweep. |
| [benchmark_discipline.md](benchmark_discipline.md) | Bench binary build, JSON artifact, throttle-storm caveats, USERAGENT, comparison recipes. |
| [tsan_discipline.md](tsan_discipline.md) | `dev-unix-tsan` (no Windows), TSAN_OPTIONS, WS-upload surface filter, NEW-vs-develop classification, expansion ideas. |
| [hard_rules_catalog.md](hard_rules_catalog.md) | Single canonical table HR1..HR49 — name, rule, rationale, origin fu7-N. |
| [fu7_N_session_template.md](fu7_N_session_template.md) | Copy-paste FollowupRequest.md skeleton for the next session. |
| [glossary.md](glossary.md) | SDK-5360 acronyms — SLU, T1, IP, MN, B9, fu7-N, HR<N>, etc. |
| [portable_template/](portable_template/README.md) | Adoption scaffold for OTHER repos. |

## How this body of work was built

Across ~17 followup sessions on SDK-5360 (`feature/SDK-5360_Websockets-uploads`)
the team converged on a small set of repeatable practices:

1. Every session writes a `FollowupRequest.md` (input) and a
   `FollowupVerdict.md` (output) under
   `~/investigationTests/.../followup7-N/`.
2. Persistent observations land in `~/.claude/projects/.../memory/` as
   `feedback_*.md` / `project_*.md` / `reference_*.md`.
3. Each fu7-N adds at most a handful of "hard rules" (HR<N>); these are
   carried forward and accreted into a single catalog.
4. No commit lands without the relevant regression sweep cells.
5. The agent does NOT push — the user re-signs commits (GPG, HR1) and
   pushes manually at session close.

## Pointers back into the rest of the docs tree

- [docs/guides/](../guides/README.md) — single guides index (CI, tests,
  benchmarks, methodology).
- [docs/ci/CI_TESTING_GUIDE.md](../ci/CI_TESTING_GUIDE.md) — Jenkins
  trigger phrases, preset glossary, artifact diffs.
- [tests/integration/BENCHMARKS.md](../../tests/integration/BENCHMARKS.md)
  — benchmark cell catalog (live operational reference).
- [CLAUDE.md](../../CLAUDE.md) — codebase guidance (build/test/style).
- [AGENTS.md](../../AGENTS.md) — environment, log locations,
  `environment2.txt`.
