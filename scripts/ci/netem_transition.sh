#!/usr/bin/env bash
# netem_transition.sh — flip the live netem parameters of a running netem_profile.sh
# namespace mid-run (SDK-5360 fu8 S8: the bad->clean transition cell proving the
# gate-v2 TRIM path — v1 parked at peak conns/RSS forever after a loss episode).
#
# Usage: netem_transition.sh <wait_s> <delay_ms> <loss_pct> <net_kbps> [ns_name]
#   wait_s   seconds to sleep AFTER the target namespace appears before flipping
#   delay/loss/net_kbps  the NEW netem params (same meaning as net_profiles.env
#            fields 1/2/4). At least one must be nonzero: `tc qdisc change` needs
#            an existing netem qdisc, and a fully-clean target would require `del`
#            which the paired start profile may not have created symmetrically.
#   ns_name  explicit namespace (default: newest megaqa_* — fine when HR56 serial
#            discipline guarantees a single active bench).
#
# Runs alongside (NOT inside) bench_matrix_runner.sh: launch it in the background
# just before the runner starts the transition cell; it polls for the namespace,
# waits, flips both veth ends, and prints a provenance banner (config==used: grep
# "netem_transition.sh: APPLIED" next to the cell's netem banner).
set -u

WAIT_S="${1:?wait_s}"; DELAY_MS="${2:?delay_ms}"; LOSS_PCT="${3:?loss_pct}"; NET_KBPS="${4:?net_kbps}"
NS="${5:-}"

for v in WAIT_S DELAY_MS LOSS_PCT NET_KBPS; do
    [[ "${!v}" =~ ^[0-9]+$ ]] || { echo "netem_transition.sh: $v not numeric: ${!v}" >&2; exit 2; }
done
if [[ "$DELAY_MS" -eq 0 && "$LOSS_PCT" -eq 0 && "$NET_KBPS" -eq 0 ]]; then
    echo "netem_transition.sh: all-zero target unsupported (qdisc change needs netem args)" >&2; exit 2
fi

# Wait (bounded) for the namespace to exist — the runner creates it per cell.
for _ in $(seq 1 300); do
    if [[ -z "$NS" ]]; then
        NS="$(ip netns list 2>/dev/null | awk '/^megaqa_/{print $1}' | tail -1)"
    fi
    [[ -n "$NS" ]] && sudo ip netns exec "$NS" true 2>/dev/null && break
    NS="${5:-}"; sleep 1
done
[[ -n "$NS" ]] || { echo "netem_transition.sh: no megaqa_* namespace appeared" >&2; exit 3; }

PID="${NS#megaqa_}"; NSVETH="vqa${PID}n"; HVETH="vqa${PID}h"
echo "netem_transition.sh: ns=$NS armed; flipping in ${WAIT_S}s to delay=${DELAY_MS}ms loss=${LOSS_PCT}% netkbps=${NET_KBPS}"
sleep "$WAIT_S"

NETEM_ARGS=()
[[ "$DELAY_MS" -ne 0 ]] && NETEM_ARGS+=(delay "${DELAY_MS}ms")
[[ "$LOSS_PCT" -ne 0 ]] && NETEM_ARGS+=(loss "${LOSS_PCT}%")
[[ "$NET_KBPS" -ne 0 ]] && NETEM_ARGS+=(rate "${NET_KBPS}kbit")

rc=0
sudo ip netns exec "$NS" tc qdisc change dev "$NSVETH" root netem "${NETEM_ARGS[@]}" || rc=$?
sudo tc qdisc change dev "$HVETH" root netem "${NETEM_ARGS[@]}" || rc=$?
if [[ $rc -eq 0 ]]; then
    echo "netem_transition.sh: APPLIED ns=$NS at $(date -u +%H:%M:%SZ) delay=${DELAY_MS}ms loss=${LOSS_PCT}% netkbps=${NET_KBPS}"
else
    echo "netem_transition.sh: FAILED (rc=$rc) ns=$NS — cell's transition did NOT apply; treat the run as start-profile-only" >&2
fi
exit $rc
