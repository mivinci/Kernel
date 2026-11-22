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
    exec "$shell" -c 'echo "$$ $PPID"; exit 37'
) > "$scratch/pids" &
child=$!
status=0
wait "$child" || status=$?
[ "$status" = 37 ] || fail "wait $child returned $status, expected 37"
read -r reported_child reported_parent < "$scratch/pids" || fail 'missing child report'
[ "$reported_child" = "$child" ] || fail "child reports $reported_child, expected $child"
[ "$reported_parent" = "$$" ] || fail "child reports parent $reported_parent, expected $$"

# A host smoke run can check the script but cannot prove wide PID preservation.
if [ "${3:-wide}" = wide ]; then
    case "$child" in
        ???????????*) ;;
        *) fail "expected a child PID wider than 32 bits, got $child" ;;
    esac
fi
echo 'busybox-pid: PASS'
