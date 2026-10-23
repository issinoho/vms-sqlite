#!/usr/bin/env bash
# installcheck.sh <node> - install the node's kit, verify it, build and run a
# program against the installed library (clang and the object library; VSI C
# and SQLITE3$SHR), run the shell from it, and remove the kit again.  This
# changes the system while it runs (PCSI database, SYS$COMMON:[VMSSQLITE],
# VMSSQLITE$ROOT and SQLITE3$SHR); run kit.sh first.  Ask the user first.
# Output: out/install-<node>.txt.
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: installcheck.sh <node>}
. "$top/upstream.conf"
REMOTE=$(echo "$UPSTREAM_NAME-$UPSTREAM_VERSION" | tr . _ | tr a-z A-Z)
"$top/tools/vms.sh" "$node" put "$top/tools/vms_installcheck.com" >/dev/null
read -r _ _ _ _ _ WORKDIR _ < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
mkdir -p "$top/out"
log=$top/out/install-$node.txt
job=$top/cache/installcheck-$node.com
printf '$ set noon\n$ @%sVMS_INSTALLCHECK.COM %s\n' "$WORKDIR" "$REMOTE" > "$job"
VMS_TIMEOUT=1800 "$top/tools/vms.sh" "$node" run "$job" > "$log" 2>&1 || true
grep -aE 'install status|startup procedure|KIT_TEST|SHELL:|remove status|after removal' "$log"
[ "$(grep -ac 'KIT_TEST: PASS' "$log")" -eq 2 ] &&
    grep -aq 'SHELL: PASS' "$log" &&
    grep -aq 'VMSSQLITE\$ROOT after removal: \[\]' "$log" &&
    grep -aq 'SQLITE3\$SHR after removal: \[\]' "$log" &&
    grep -aq 'files after removal: \[\]' "$log" &&
    grep -aq 'startup after removal: \[\]' "$log"
