#!/usr/bin/env bash
# procs.sh <node> - list our account's processes on <node>: PID, name, image,
# CPU (10 ms units).  Run after any vms.sh timeout or stopped job: the remote
# session goes on (CLAUDE.md, PORTING_LOG #17, #23).  Our own sessions are
# FTA<n>_<user> (vms.sh) and their subprocesses <user>_<n>; a session running a
# .COM of ours has a vmsrun_*.COM without a .DONE in the work directory.  Stop
# only what is ours: the account may also be the user's own (interactive
# sessions, scheduled jobs such as "sshlogs", other projects' builds).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
node=${1:?usage: procs.sh <node>}
com=$(mktemp --suffix=.com)
trap 'rm -f "$com"' EXIT
cat > "$com" <<'EOF'
$ set noon
$ ctx = ""
$loop:
$ pid = f$pid(ctx)
$ if pid .eqs. "" then goto done
$ if f$getjpi(pid,"USERNAME") .nes. f$getjpi("","USERNAME") then goto loop
$ write sys$output pid, " ", f$getjpi(pid,"PRCNAM"), " ", f$getjpi(pid,"IMAGNAME"), " cpu ", f$getjpi(pid,"CPUTIM")
$ goto loop
$done:
$ write sys$output "--- vms.sh sessions' procedures (a finished one has a .DONE beside it):"
$ directory/date=created/nohead/notrail vmsrun_*.*
EOF
"$here/vms.sh" "$node" run "$com" | grep -v '^\s*$'
