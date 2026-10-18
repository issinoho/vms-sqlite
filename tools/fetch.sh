#!/usr/bin/env bash
# fetch.sh - download the SQLite release files named in upstream.conf into cache/
# and verify their SHA3-256 hashes (SQLite publishes no signatures: D1).
set -euo pipefail

top=$(cd "$(dirname "$0")/.." && pwd)
. "$top/upstream.conf"
cache=$top/cache
mkdir -p "$cache"

sha3() { python3 -I -c 'import hashlib,sys; print(hashlib.sha3_256(open(sys.argv[1],"rb").read()).hexdigest())' "$1"; }

get() {  # get <url> <sha3>
    local f=$cache/$(basename "$1")
    [ -f "$f" ] || { echo "fetch: downloading $1"
                     curl -fsSL -o "$f.tmp" "$1"; mv "$f.tmp" "$f"; }
    [ "$(sha3 "$f")" = "$2" ] || { echo "fetch: SHA3-256 mismatch for $f" >&2; exit 1; }
    echo "fetch: $f (SHA3-256 OK)"
}
get "$UPSTREAM_URL" "$UPSTREAM_SHA3"
get "$TESTSRC_URL" "$TESTSRC_SHA3"
