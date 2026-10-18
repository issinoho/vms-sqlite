#!/usr/bin/env bash
# push.sh <node> - upload the prepared tree (staging/) to <node>'s work directory.
#
# Uploads what the VMS build and tests need (the amalgamation, vmsport) into
# <workdir>/<name>-<version with dots as underscores>, e.g. [.GREP-3_12].
# Re-pushing uploads new file versions; build.sh purges the old ones on VMS.
set -euo pipefail
export LC_ALL=C   # sort and comm must agree on collation

top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: push.sh <node>}
. "$top/upstream.conf"
name=$UPSTREAM_NAME-$UPSTREAM_VERSION
stage=$top/staging/$name
remote=$(echo "$name" | tr . _)
[ -d "$stage/vmsport" ] || { echo "push: run tools/prepare.sh first" >&2; exit 1; }

pack=$top/cache/push-$name-$node
rm -rf "$pack"; mkdir -p "$pack/$remote"
# The amalgamation is all the VMS build uses; the autoconf files stay here.
( cd "$stage" && cp -t "$pack/$remote/" sqlite3.c sqlite3.h sqlite3ext.h shell.c VERSION README.txt ) &&
    cp -a "$stage/vmsport" "$pack/$remote/"


echo "push: -> $node:[.$(echo "$remote" | tr a-z A-Z)]"
read -r _ _ HOST PORT USER WORKDIR SFTPDIR < <(awk -v n="$node" '$1==n' "$top/tools/nodes.conf")
# Upload only files whose content changed since the last push to this node,
# so MMS sees new timestamps only on what really changed.  Explicit put per
# file: 'put -r' into an existing directory nests a copy instead.
manifest=$top/cache/pushed-$name-$node.sha
[ "${PUSH_ALL:-}" = 1 ] && rm -f "$manifest"
touch "$manifest"
(cd "$pack" && find "$remote" -type f -print0 | sort -z | xargs -0 sha256sum) > "$pack.sha"
changed=$(comm -23 <(awk '{print $2" "$1}' "$pack.sha" | sort) \
                   <(awk '{print $2" "$1}' "$manifest" | sort) | awk '{print $1}')
echo "push: $(echo "$changed" | grep -c . || true) changed of $(wc -l < "$pack.sha") files"
batch=$pack/sftp.batch
{
    echo "cd $SFTPDIR"
    (cd "$pack" && find "$remote" -type d) | sed 's/^/-mkdir /'
    for f in $changed; do echo "put $pack/$f $f"; done
} > "$batch"
sftp -P "$PORT" -i "${VMS_SSH_KEY:-$HOME/.ssh/vms_ed25519}" -o BatchMode=yes -b "$batch" "$USER@$HOST" \
    2>&1 >/dev/null | grep -vE '^ *Welcome to|^ *$|^remote mkdir .*Failure' >&2 || true
cp "$pack.sha" "$manifest"
echo "push: done"
