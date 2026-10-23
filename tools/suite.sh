#!/usr/bin/env bash
# suite.sh <node> CLANG|CC [quick] - the whole regression run of one build, in
# order, one job at a time (never two node jobs at once: mptest and speedtest1
# measure time): VFS_TEST, CONCURRENT, MPTEST (all four scripts), KILLTEST, the
# SQL corpus (tools/corpus.sh) and, unless "quick", speedtest1 (SPEEDTEST.COM).
# The full output goes to out/suite-<node>-<comp>.log; a summary is printed.
# Build first (tools/build.sh <node> ALL <comp>).  ~1 hour with speedtest1.
#
# What "passing" means (docs/PHASE2.md): VFS: 0 failures, P0 growth 0;
# CONCURRENT: all 4000 rows; mptest: 0 errors in every Summary (config02 can
# still time out under load: rerun it alone, and repeat it ~15 times before
# calling a corruption fix good, PORTING_LOG #25); KILLTEST: 20 CONSISTENT;
# corpus: identical; no "malformed"/"corrupt" anywhere.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
node=${1:?usage: suite.sh <node> CLANG|CC [quick]}
comp=$(echo "${2:?usage: suite.sh <node> CLANG|CC [quick]}" | tr a-z A-Z)
log=$top/out/suite-$node-$(echo "$comp" | tr A-Z a-z).log
mkdir -p "$top/out"
. "$top/upstream.conf"
t="@[.SQLITE-$(echo "$UPSTREAM_VERSION" | tr . _).VMSPORT.TEST]"
run() { echo "=== $*" >> "$log"; VMS_TIMEOUT=${VMS_TIMEOUT:-3600} "$@" >> "$log" 2>&1; }
: > "$log"
run "$here/vms.sh" "$node" dcl "${t}VFS_TEST $comp"
run "$here/vms.sh" "$node" dcl "${t}CONCURRENT $comp"
run "$here/vms.sh" "$node" dcl "${t}MPTEST $comp"
run "$here/vms.sh" "$node" dcl "${t}KILLTEST $comp"
run "$here/corpus.sh" "$node" "$comp"
[ "${3:-}" = quick ] || run "$here/vms.sh" "$node" dcl "${t}SPEEDTEST $comp"
echo "suite $node $comp ($log):"
grep -E "^=== |VFS:|VFS FAIL|P0 growth|CONCURRENT:|Summary|corpus:|timed out after|TOTAL\.\.\." "$log" |
    sed 's/DISK\$[^ ]*//; s/^=== .*\(VFS_TEST\|CONCURRENT\|MPTEST\|KILLTEST\|corpus.sh\|SPEEDTEST\).*/--- \1/'
echo "KILLTEST consistent: $(grep -c 'CONSISTENT.* ok' "$log") of 20; corrupt/malformed lines: $(grep -ciE 'malformed|corrupt' "$log")"
echo "Now check for leftover processes: tools/procs.sh $node"
