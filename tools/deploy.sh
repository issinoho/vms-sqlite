#!/usr/bin/env bash
# deploy.sh <node> [CLANG|CC] - copy the built install tree to the shared
# dependency directory that other ports (vms-php) build against (D10).
#
#   <workdir>[.SQLITE-<ver>.INSTALL_<arch>[_CLANG].{INCLUDE,LIB,BIN}]
#     -> <depsdir>[.SQLITE-<ver>.INSTALL_<arch>[_CLANG].{INCLUDE,LIB,BIN}]
#
# <depsdir> is column 8 of tools/nodes.conf (as in vms-php), e.g. the
# directory where vms-zlib, vms-pcre2 and vms-libxml2 are built.  Run after
# each build that should reach PHP.  Copies make new file versions; nothing
# in <depsdir> is deleted or purged.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
node=${1:?usage: deploy.sh <node> [CLANG|CC]}
comp=$(echo "${2:-CLANG}" | tr a-z A-Z)
. "$top/upstream.conf"
read -r _ ARCH _ _ _ _ _ DEPSDIR < <(awk -v n="$node" '$1==n' "$here/nodes.conf") || true
[ -n "${DEPSDIR:-}" ] || { echo "deploy.sh: no depsdir (column 8) for '$node' in tools/nodes.conf" >&2; exit 2; }
tree="SQLITE-$(echo "$UPSTREAM_VERSION" | tr . _)"
inst="INSTALL_${ARCH}"; [ "$comp" = CLANG ] && inst="${inst}_CLANG"
dst="${DEPSDIR%]}.${tree}.${inst}"
cmds=("set noon")
for d in INCLUDE LIB BIN; do
    cmds+=("create/directory ${dst}.${d}]"
           "copy/log [.${tree}.${inst}.${d}]*.*;0 ${dst}.${d}]")
done
cmds+=("directory/size/date ${dst}...]")
"$here/vms.sh" "$node" dcl "${cmds[@]}"
