#!/usr/bin/env bash
# corpus.sh <node> [CLANG|CC] - run the SQL corpus (overlay/vmsport/test/corpus)
# on the host and on <node>, and compare the outputs.  The host's shell is built
# from the staged amalgamation with the VMS build's options (D4), on its unix VFS.
# Results: out/corpus/<compiler>/ (host/, vms/, diff.txt).
set -euo pipefail
top=$(cd "$(dirname "$0")/.." && pwd)
node=${1:?usage: corpus.sh <node> [CLANG|CC]}
comp=${2:-CLANG}
. "$top/upstream.conf"
stage=$top/staging/$UPSTREAM_NAME-$UPSTREAM_VERSION
remote=$(echo "$UPSTREAM_NAME-$UPSTREAM_VERSION" | tr . _)
res=$top/out/corpus/$(echo "$comp" | tr A-Z a-z)
mkdir -p "$res/host" "$res/vms" "$top/cache/host"
host=$top/cache/host/sqlite3
if [ ! -x "$host" ] || [ "$stage/sqlite3.c" -nt "$host" ]; then
    echo "corpus: building the host shell"
    gcc -O2 -DSQLITE_THREADSAFE=1 -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_OMIT_LOAD_EXTENSION \
        -DSQLITE_MAX_MMAP_SIZE=0 -DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_RTREE \
        -DSQLITE_ENABLE_MATH_FUNCTIONS "$stage/shell.c" "$stage/sqlite3.c" -lpthread -lm -o "$host"
fi
work=$(mktemp -d -p "$top/cache" corpus.XXXX)
for f in "$stage"/vmsport/test/corpus/*.sql; do
    n=$(basename "$f" .sql)
    (cd "$work" && "$host" "$n.db" < "$f" > "$res/host/$n.out" 2> "$res/host/$n.err") || true
done
"$top/tools/push.sh" "$node" >/dev/null
VMS_TIMEOUT=3600 "$top/tools/vms.sh" "$node" dcl "@[.$(echo "$remote" | tr a-z A-Z).VMSPORT.TEST]CORPUS $comp" | grep -c 'CORPUS: ' >/dev/null
for f in "$stage"/vmsport/test/corpus/*.sql; do
    n=$(basename "$f" .sql); u=$(echo "$n" | tr a-z A-Z)
    for x in out err; do
        "$top/tools/vms.sh" "$node" get "$remote/vmsport/test/RUN/CORPUS_$comp/$u.$(echo $x | tr a-z A-Z)" "$res/vms/$n.$x" 2>/dev/null || : > "$res/vms/$n.$x"
        sed -i 's/\r$//' "$res/vms/$n.$x"
    done
done
diff -r "$res/host" "$res/vms" > "$res/diff.txt" && echo "corpus: $comp identical to the host ($(ls "$res/host" | grep -c out) scripts)" ||
    { echo "corpus: $comp differs from the host:"; diffstat < "$res/diff.txt" 2>/dev/null || grep -c '^[<>]' "$res/diff.txt"; }
