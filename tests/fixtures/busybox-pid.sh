#!/bin/sh
# Run with BusyBox ash, passing an empty scratch directory owned by the test.
set -eu

scratch=${1:?empty scratch directory required}
shell=${2:-sh}

fail()
{
    echo "busybox-pid: $*" >&2
    exit 1
}

(
    exec "$shell" -c 'kill -0 "$$" && kill -0 "$PPID" || exit 38; echo "$$ $PPID"; exit 37'
) > "$scratch/pids" &
child=$!
status=0
wait "$child" || status=$?
[ "$status" = 37 ] || fail "wait $child returned $status, expected 37"
read -r reported_child reported_parent < "$scratch/pids" || fail 'missing child report'
[ "$reported_child" = "$child" ] || fail "child reports $reported_child, expected $child"
[ "$reported_parent" = "$$" ] || fail "child reports parent $reported_parent, expected $$"

for pid in "$child" "$reported_parent"; do
    [ "$pid" -ge 1 ] && [ "$pid" -le 2147483647 ] || fail "PID outside positive 32-bit range: $pid"
done
echo 'busybox-pid: PASS'
