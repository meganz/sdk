# Followup workflow — the fu7-N session pattern

A "followup" session is a bounded unit of work on a long-running branch
(e.g. `feature/SDK-5360_Websockets-uploads`). Each session has an input
file, a verdict file, a per-Goal directory structure, and a defined
hand-off contract with the user.

## The loop

```
FollowupRequest.md            (user input — what to do)
        |
        v
   plan-mode plan             (agent proposes the goal breakdown)
        |
        v
  ExitPlanMode → approval
        |
        v
   execute Goals 0..N         (one or more agents, sequentially)
        |
        v
   FollowupVerdict.md         (closing report)
        |
        v
   memory update              (MEMORY.md + new feedback_/project_ entries)
        |
        v
   user re-signs commits      (HR1; GPG pinentry doesn't work in agents)
        |
        v
   user pushes                (HR1; never the agent)
```

## Per-Goal directory structure

```
~/investigationTests/.../followup7-N/
  FollowupRequest.md
  FollowupVerdict.md
  session_timestamps.log
  Goal0_audit/
    fu7_<N-1>_audit.md
    fu7_N_bench_trajectory.md
    negative_observations.md
    mn_envelope_trend.md
  Goal1_<name>/
    investigation_report.md
    implementation_log.md
  Goal2_<name>/
    ...
  GoalK_methodology_preservation/   # e.g. Goal 7 in fu7-17
```

Each Goal directory holds the artefacts that justify the Verdict claims:
audit reports, bench JSONs, logs, comparison tables.

## Verdict labels

The Verdict file ends with one of:

| Label | Meaning |
|---|---|
| `VALIDATED_TECHQA_READY` | All Goals DONE; sweep ≥80% PASS; no NEW TSAN; ready for hand-off. |
| `PARTIAL_DEFER` | One or more Goals deferred with explicit HR-compatible justification. |
| `REGRESSION_DETECTED` | A fix or refactor introduced a regression; revert + escalate. |
| `BLOCKED_ON_USER` | Decision requires user input (e.g. header relocation policy, branch switch). |
| `BLOCKED_ON_INFRA` | Test infra, accounts, or upstream service prevented closure. |

## Hand-off contract (HR1)

- The agent **never** pushes. GPG pinentry doesn't work in non-interactive
  subprocesses; commits land unsigned, the user re-signs at session
  close.
- The agent may force-push back the same branch ONLY if the user has
  explicitly authorised it in the current session (and never to
  `develop`/`main`).
- The agent leaves the branch on the HEAD that the user will sign and
  push.

## Example sessions to read

- **fu7-16** (`…/followup7-16/`) — heavy modularisation session; the
  `Goal2_cross_followup_refactor_sweep/domain_coupling_map.md` is the
  canonical example of how an investigator pre-plans an extraction
  sequence.
- **fu7-13** (`…/followup7-13/`) — example of catching a NEW TSAN race
  via the `Goal4` deep-dive and adding HR42 (`dev-unix-strict`).
- **fu7-15** (`…/followup7-15/`) — example of timing-cell stress
  discipline that motivated HR43/HR46 (`--gtest_repeat=15` +
  `taskset -c 0 nice -n 19`).

## Adding a new Goal

When a goal emerges mid-session that wasn't in `FollowupRequest.md`:

1. Note it in the Verdict under a "Goal X (added in-session)" header.
2. Create the `GoalX_<name>/` directory and add a short rationale file.
3. Document the carry-forward (if any) in the next session's
   `FollowupRequest.md`.

## Closing a session

1. Run the final regression sweep per `regression_sweep_cadence.md`.
2. Write `FollowupVerdict.md` with per-Goal status + final sweep numbers
   + label.
3. Update `MEMORY.md` with a 1-line index entry pointing to a new
   `project_sdk5360_followupN_results.md` (see `memory_discipline.md`).
4. Commit the artefacts (audit dir + memory) on a working tree separate
   from the SDK repo — they live under `~/investigationTests/...`, not
   in the SDK repo itself.
5. Hand off to the user with: branch name, HEAD hash, list of held
   commits awaiting re-sign, list of any user-action items.
