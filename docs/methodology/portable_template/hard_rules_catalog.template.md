# Hard rules catalog — HR1..HR<N>

The canonical list of operational hard rules accumulated as your
project encounters incidents that motivate them. Each rule has an ID,
a name, a one-line rule statement, a one-sentence rationale, and the
session / incident that introduced it.

IDs are stable references in session reports and verdicts. Do not
renumber retroactively.

## Catalog

| HR | Name | Rule | Rationale | Origin |
|---|---|---|---|---|

<!--
Add rows as your team encounters incidents.

Example row:
| HR1 | Agent does not push | The agent never executes git push; user re-signs and pushes. | Pinentry doesn't work in non-interactive subprocesses. | session-001 |
-->

## Reading the catalog

Once populated, classify rules into:

- **Hot path** (every session uses): the rules every session must
  satisfy.
- **Per-major-change**: rules that fire on cross-cutting refactors.
- **Defensive / compile**: pre-push gates, strict-warning presets.
- **Hygiene / documentation**: process-level rules about how outputs
  are formatted.

## Adding a new HR

A new HR is justified when:

1. A session encountered a class of regression / mistake that a
   one-line rule would have prevented.
2. The fix is mechanical or process-level — not a code change.
3. The team agrees the rule applies to all future similar work.

When adding:

- Append the row at the end (don't insert mid-table).
- Reference the incident commit hash + session.
- If the project has a memory directory, add a `feedback_hr<N>_<topic>.md`
  entry too.

## Removing an HR

A rule may be retired when:

1. The underlying tooling / preset / process is permanently gone.
2. A stronger rule subsumes it.

Do not delete the row — mark with `~~strike~~` and add a note in the
"Origin" cell pointing to the superseding rule.

## Inspiration

See the SDK-5360 `hard_rules_catalog.md` for a live example with ~49
rules covering bench discipline, TSAN discipline, push protocol,
memory hygiene, and per-Goal sweep cadence.
