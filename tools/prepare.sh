#!/usr/bin/env bash
# prepare.sh - build a VMS-ready SQLite source tree in staging/<name>-<version>/
#
#   1. fetch + verify the release (SHA3-256, D1)
#   2. extract the amalgamation, apply patches/series, lay overlay/ over the top
#
# SQLite builds on VMS from the amalgamation with our [.VMSPORT]BUILD.COM; the
# autoconf files are not used.  Nothing in staging/ is ever edited by hand:
# fix things in patches/ or overlay/.
set -euo pipefail

top=$(cd "$(dirname "$0")/.." && pwd)
. "$top/upstream.conf"
name=$UPSTREAM_NAME-$UPSTREAM_VERSION
tarball=$top/cache/$(basename "$UPSTREAM_URL")
stage=$top/staging/$name

step() { echo "prepare: $*"; }
die() { echo "prepare: error: $*" >&2; exit 1; }

"$top/tools/fetch.sh" >/dev/null

step "extracting $name"
rm -rf "$stage" "$top/staging/$(basename "$tarball" .tar.gz)"; mkdir -p "$top/staging"
tar -xzf "$tarball" -C "$top/staging"
mv "$top/staging/$(basename "$tarball" .tar.gz)" "$stage"

# SQLite's multi-process, thread and speed tests, from the full source zip (D1)
step "test programs from $(basename "$TESTSRC_URL")"
unzip -q -o "$top/cache/$(basename "$TESTSRC_URL")" -d "$top/staging/tsrc.$$"
tsrc=$top/staging/tsrc.$$/$(basename "$TESTSRC_URL" .zip)
mkdir -p "$stage/test"
cp -a "$tsrc/mptest" "$stage/"
cp "$tsrc/test/speedtest1.c" "$tsrc/test/threadtest4.c" "$stage/test/"
rm -rf "$top/staging/tsrc.$$"

while read -r p; do
    case $p in ''|'#'*) continue ;; esac
    step "patch $p"
    patch -d "$stage" -p1 -s --no-backup-if-mismatch -F0 < "$top/patches/$p" ||
        die "patch $p does not apply cleanly"
done < "$top/patches/series"

# overlay/ may only add files; changes to upstream files belong in patches/.
(cd "$top/overlay" && find . -type f) | while read -r f; do
    [ -e "$stage/$f" ] && die "overlay/$f would replace an upstream file; use a patch"
    true
done
cp -a "$top/overlay/." "$stage/"

step "shareable image options"
"$top/tools/gen_shr_opt.sh" "$stage"

printf 'VERSION=%s\nKIT_VERSION=%s-vms%s\n' "$UPSTREAM_VERSION" "$UPSTREAM_VERSION" \
    "$VMS_PATCH_LEVEL" > "$stage/vmsport/version.env"
step "staged $stage"
