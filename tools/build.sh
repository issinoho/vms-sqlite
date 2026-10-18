#!/usr/bin/env bash
# build.sh <node> [ALL|CLEAN] [CLANG|CC] - push the prepared tree and run
# [.VMSPORT]BUILD.COM on <node>.  The log is printed and saved to
# out/build-<node>-<compiler>.log.
set -euo pipefail

top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: build.sh <node> [ALL|CLEAN] [CLANG|CC]}
target=${2:-ALL}
comp=${3:-CLANG}
. "$top/upstream.conf"
remote=$(echo "$UPSTREAM_NAME-$UPSTREAM_VERSION" | tr . _ | tr a-z A-Z)
read -r _ _ _ _ _ WORKDIR _ < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")

"$top/tools/push.sh" "$node"
mkdir -p "$top/out"
job=$top/cache/build-$node.com
cat > "$job" <<DCL
\$ set noon
\$ set process/parse_style=extended
\$ purge/nolog ${WORKDIR%]}.$remote...]*.*
\$ @${WORKDIR%]}.$remote.VMSPORT]BUILD.COM "$target" "$comp"
DCL
log=$top/out/build-$node-$(echo "$comp" | tr A-Z a-z).log
VMS_TIMEOUT=${VMS_BUILD_TIMEOUT:-3600} "$top/tools/vms.sh" "$node" run "$job" | tee "$log"
grep -q 'BUILD: done' "$log"
