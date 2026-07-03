#!/usr/bin/env bash
#
# netem_profile.sh — run a command inside a network namespace whose egress is
# shaped (delay / loss) per a named profile, for SDK-5360 WS-upload poor-network
# reproduction.
#
# USAGE (run AS THE NORMAL USER — do NOT prefix the script with sudo):
#   scripts/ci/netem_profile.sh --profile <name> -- <command...>
#
# Example:
#   scripts/ci/netem_profile.sh --profile qaexact -- \
#       /path/to/test_integration --gtest_filter='SdkBenchmarkTest.QaExactSingleFile' \
#       --CI --COUT --USERAGENT:JenkinsCanSpam-SDK
#
# PRIVILEGE MODEL (validated against real MEGA via a network namespace):
#   * The script itself runs as the invoking user. Running the whole thing as
#     root would give test_integration HOME=/root and lose MEGA_EMAIL/MEGA_PWD.
#   * Each privileged op uses per-command `sudo` (passwordless in this env).
#   * The CHILD command runs as the invoking user INSIDE the namespace, with the
#     caller's environment preserved (HOME/MEGA_EMAIL/MEGA_PWD/...), adding only
#     MEGA_NET_PROFILE and MEGA_NET_MAXUPLOAD_KBPS:
#       sudo ip netns exec <ns> sudo -u "$USER" --preserve-env \
#           env MEGA_NET_PROFILE=<name> MEGA_NET_MAXUPLOAD_KBPS=<kbps> <command...>
#
# NETEM MECHANICS:
#   * veth pair: host end <hveth> (10.211.7.1/24) <-> ns end <nsveth> (10.211.7.2/24)
#   * Bidirectional shaping: netem applied to BOTH egress ends (ns end inside the
#     netns, host end on the host) so delay/loss hit each direction.
#   * Bandwidth is NOT done via netem rate; it is handed to the SDK as
#     MEGA_NET_MAXUPLOAD_KBPS (kilobits/s) so the SDK caps via setMaxUploadSpeed.
#   * MASQUERADE + FORWARD rules give the ns egress to the internet via the host
#     default interface; DNS via /etc/netns/<ns>/resolv.conf.
#
# set -e is intentionally NOT used: we must always reach teardown and propagate
# the child's exit code.

set -uo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly PROFILES_ENV="${SCRIPT_DIR}/net_profiles.env"

# --- Namespace / addressing (unique per PID) --------------------------------
readonly NS="megaqa_$$"
readonly HVETH="vqa${$}h"   # host-side veth (kept <= 15 chars)
readonly NSVETH="vqa${$}n"  # ns-side veth
readonly HOST_ADDR="10.211.7.1"
readonly NS_ADDR="10.211.7.2"
readonly SUBNET="10.211.7.0/24"
readonly PREFIX="24"
readonly DNS_SERVER="1.1.1.1"

die() {
    echo "netem_profile.sh: ERROR: $*" >&2
    exit 2
}

usage() {
    cat >&2 <<'EOF'
Usage: scripts/ci/netem_profile.sh --profile <name> -- <command...>
  Run <command...> inside a shaped network namespace.
  Profiles are defined in net_profiles.env next to this script.
  Run as the normal user (NOT via sudo); the script sudo's per-op internally.
EOF
    exit 2
}

# --- Argument parsing --------------------------------------------------------
PROFILE=""
CMD=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --profile)
            [[ $# -ge 2 ]] || usage
            PROFILE="$2"
            shift 2
            ;;
        --profile=*)
            PROFILE="${1#*=}"
            shift
            ;;
        --)
            shift
            CMD=("$@")
            break
            ;;
        -h|--help)
            usage
            ;;
        *)
            die "unexpected argument: $1 (did you forget '--' before the command?)"
            ;;
    esac
done

[[ -n "$PROFILE" ]] || usage
[[ ${#CMD[@]} -gt 0 ]] || die "no command given after '--'"
[[ -f "$PROFILES_ENV" ]] || die "profile registry not found: $PROFILES_ENV"

# The invoking (non-root) user — child must run as this user with env preserved.
RUN_USER="${SUDO_USER:-${USER:-$(id -un)}}"
[[ -n "$RUN_USER" ]] || die "could not determine invoking user"
[[ "$RUN_USER" != "root" ]] || die "do not invoke this script as root; run as the normal user (per-op sudo is used internally)"

# --- Resolve profile -> "delay_ms loss_pct kbps" -----------------------------
# net_profiles.env defines one shell variable per profile, e.g. clean="0 0 0".
# shellcheck source=/dev/null
source "$PROFILES_ENV"
PROFILE_VALUE="${!PROFILE-}"
[[ -n "$PROFILE_VALUE" ]] || die "unknown profile '$PROFILE' (not defined in $PROFILES_ENV)"

read -r DELAY_MS LOSS_PCT KBPS <<<"$PROFILE_VALUE"
: "${DELAY_MS:=0}" "${LOSS_PCT:=0}" "${KBPS:=0}"
[[ "$DELAY_MS" =~ ^[0-9]+$ ]] || die "profile '$PROFILE' delay_ms not numeric: '$DELAY_MS'"
[[ "$LOSS_PCT" =~ ^[0-9]+$ ]] || die "profile '$PROFILE' loss_pct not numeric: '$LOSS_PCT'"
[[ "$KBPS"     =~ ^[0-9]+$ ]] || die "profile '$PROFILE' kbps not numeric: '$KBPS'"

# --- Discover host default interface ----------------------------------------
DEFAULT_IFACE="$(ip route show default 2>/dev/null | awk '{print $5; exit}')"
[[ -n "$DEFAULT_IFACE" ]] || die "could not determine default network interface"

# --- Teardown (idempotent; safe to run when nothing exists) ------------------
teardown() {
    # Best-effort; never abort teardown on a single failure.
    sudo ip netns del "$NS" 2>/dev/null || true
    # Deleting the netns auto-removes the ns-side veth; remove the host end too
    # in case the pair survived (e.g. setup failed before moving the ns end).
    sudo ip link del "$HVETH" 2>/dev/null || true
    sudo iptables -t nat -D POSTROUTING -s "$SUBNET" -o "$DEFAULT_IFACE" -j MASQUERADE 2>/dev/null || true
    sudo iptables -D FORWARD -i "$HVETH" -o "$DEFAULT_IFACE" -j ACCEPT 2>/dev/null || true
    sudo iptables -D FORWARD -i "$DEFAULT_IFACE" -o "$HVETH" -j ACCEPT 2>/dev/null || true
    sudo rm -rf "/etc/netns/$NS" 2>/dev/null || true
}

# --- Global stale-state sweep (robustness) -----------------------------------
# A prior run that was killed/crashed before teardown leaves a vqa* veth still
# holding the FIXED host address 10.211.7.1; the next run's identical address
# assignment then silently collides -> the netns default route blackholes ->
# every cs/login POST times out (HTTP status 0) -> the whole bench fails at
# login with no obvious cause. Benches are strictly serial (HR56), so no live
# peer can exist: sweep EVERY leftover megaqa_*/vqa* so a dead predecessor can
# never break the next bench.
sweep_stale_global() {
    local ns vif hdl
    for ns in $(ip netns list 2>/dev/null | awk '/^megaqa_/{print $1}'); do
        sudo ip netns del "$ns" 2>/dev/null || true
    done
    for vif in $(ip -br link show type veth 2>/dev/null | awk '/^vqa/{sub(/@.*/,"",$1); print $1}'); do
        sudo ip link del "$vif" 2>/dev/null || true
    done
    # Drop now-inert FORWARD accept rules that reference any (now-deleted) vqa* veth.
    for hdl in $(sudo nft -a list chain ip filter FORWARD 2>/dev/null | awk '/"vqa/{for(i=1;i<=NF;i++) if($i=="handle") print $(i+1)}'); do
        sudo nft delete rule ip filter FORWARD handle "$hdl" 2>/dev/null || true
    done
}

# --- Pre-clean any stale state (global predecessors + self), then arm trap ----
sweep_stale_global
teardown
trap teardown EXIT INT TERM

# --- Namespace + veth setup --------------------------------------------------
sudo sysctl -w net.ipv4.ip_forward=1 >/dev/null || die "failed to enable ip_forward"

sudo ip netns add "$NS"                                  || die "ip netns add failed"
sudo ip link add "$HVETH" type veth peer name "$NSVETH"  || die "veth pair creation failed"
sudo ip link set "$NSVETH" netns "$NS"                    || die "moving ns veth into netns failed"

# Host end addressing + up.
sudo ip addr add "${HOST_ADDR}/${PREFIX}" dev "$HVETH"    || die "host veth addr failed"
sudo ip link set "$HVETH" up                              || die "host veth up failed"

# NS end: loopback + addr + up + default route via host.
sudo ip netns exec "$NS" ip link set lo up                || die "ns lo up failed"
sudo ip netns exec "$NS" ip addr add "${NS_ADDR}/${PREFIX}" dev "$NSVETH" || die "ns veth addr failed"
sudo ip netns exec "$NS" ip link set "$NSVETH" up         || die "ns veth up failed"
sudo ip netns exec "$NS" ip route add default via "$HOST_ADDR" || die "ns default route failed"

# --- Host NAT + forwarding ---------------------------------------------------
sudo iptables -t nat -A POSTROUTING -s "$SUBNET" -o "$DEFAULT_IFACE" -j MASQUERADE \
    || die "MASQUERADE rule failed"
sudo iptables -A FORWARD -i "$HVETH" -o "$DEFAULT_IFACE" -j ACCEPT \
    || die "FORWARD (ns->wan) rule failed"
sudo iptables -A FORWARD -i "$DEFAULT_IFACE" -o "$HVETH" -j ACCEPT \
    || die "FORWARD (wan->ns) rule failed"

# --- DNS inside the namespace ------------------------------------------------
sudo mkdir -p "/etc/netns/$NS"                            || die "mkdir /etc/netns/$NS failed"
echo "nameserver $DNS_SERVER" | sudo tee "/etc/netns/$NS/resolv.conf" >/dev/null \
    || die "writing ns resolv.conf failed"

# --- Bidirectional netem (delay/loss EACH WAY) -------------------------------
# Skip entirely for the 'clean' case (no delay, no loss).
if [[ "$DELAY_MS" -ne 0 || "$LOSS_PCT" -ne 0 ]]; then
    NETEM_ARGS=()
    [[ "$DELAY_MS" -ne 0 ]] && NETEM_ARGS+=(delay "${DELAY_MS}ms")
    [[ "$LOSS_PCT" -ne 0 ]] && NETEM_ARGS+=(loss "${LOSS_PCT}%")
    # ns-end egress (inside the netns)
    sudo ip netns exec "$NS" tc qdisc add dev "$NSVETH" root netem "${NETEM_ARGS[@]}" \
        || die "tc netem on ns veth failed"
    # host-end egress (on the host)
    sudo tc qdisc add dev "$HVETH" root netem "${NETEM_ARGS[@]}" \
        || die "tc netem on host veth failed"
fi

# --- Banner ------------------------------------------------------------------
echo "netem_profile.sh: profile=${PROFILE} delay=${DELAY_MS}ms loss=${LOSS_PCT}% kbps=${KBPS} iface=${DEFAULT_IFACE} ns=${NS} user=${RUN_USER}"

# --- Run the child inside the namespace, as the invoking user ----------------
# The first `sudo ip netns exec` runs as ROOT and strips the caller's env, so we
# CANNOT rely on --preserve-env (it would leak root's HOME=/root and drop the
# MEGA_* creds). Instead snapshot the vars the child needs HERE (the script runs
# as the invoking user, so $HOME and $MEGA_* are correct) and re-inject them as
# explicit `env` arguments — they travel as argv, immune to sudo env-stripping.
CALLER_HOME="${HOME:-$(getent passwd "$RUN_USER" | cut -d: -f6)}"
# Pass through every MEGA_* the caller exported (MEGA_EMAIL/MEGA_PWD/_AUX/...);
# MEGA_NET_* are set explicitly below and override any captured value.
PASS_ENV=()
while IFS= read -r kv; do PASS_ENV+=("$kv"); done < <(env | grep -E '^MEGA_' | grep -vE '^MEGA_NET_(PROFILE|MAXUPLOAD_KBPS)=' || true)

sudo ip netns exec "$NS" \
    sudo -u "$RUN_USER" \
    env -i \
        HOME="$CALLER_HOME" \
        USER="$RUN_USER" \
        LOGNAME="$RUN_USER" \
        PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" \
        LANG="${LANG:-C.UTF-8}" \
        "${PASS_ENV[@]}" \
        "MEGA_NET_PROFILE=${PROFILE}" "MEGA_NET_MAXUPLOAD_KBPS=${KBPS}" \
        "${CMD[@]}"
CHILD_RC=$?

# Teardown runs via the EXIT trap; propagate the child's exit code.
exit "$CHILD_RC"
