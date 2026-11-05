#!/usr/bin/env bash
#
# fetch_busybox.sh -- download the pinned official static x86_64 busybox.
#
# The interactive-shell initramfs needs a statically linked x86_64 busybox.
# We use the official prebuilt musl binary from busybox.net (no cross build,
# works on any host), verify its sha256 and keep it under build/ so nothing
# binary is committed.
#
#   binary:  busybox 1.35.0, x86_64-linux-musl, statically linked, stripped
#   url:     https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox
#   sha256:  6e123e7f3202a8c1e9b1f94d8941580a25135382b99e8d3e34fb858bba311348
#
# Usage:
#   tools/linux/fetch_busybox.sh [--out PATH] [--force]
#
set -euo pipefail

VERSION="1.35.0"
FLAVOUR="x86_64-linux-musl"
URL="https://busybox.net/downloads/binaries/${VERSION}-${FLAVOUR}/busybox"
SHA256="6e123e7f3202a8c1e9b1f94d8941580a25135382b99e8d3e34fb858bba311348"

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUT="$REPO_ROOT/build/linux/busybox"
FORCE=0

while [[ $# -gt 0 ]]; do
	case "$1" in
	--out) OUT="$2"; shift 2 ;;
	--force) FORCE=1; shift ;;
	-h|--help) sed -n '2,19p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
	*) echo "error: unknown argument: $1" >&2; exit 2 ;;
	esac
done

log() { printf '==> %s\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

sha() {
	if command -v sha256sum >/dev/null 2>&1; then
		sha256sum "$1" | cut -d' ' -f1
	else
		shasum -a 256 "$1" | cut -d' ' -f1
	fi
}

if [[ "$FORCE" == 0 && -f "$OUT" ]]; then
	have=$(sha "$OUT")
	if [[ "$have" == "$SHA256" ]]; then
		log "busybox ${VERSION} (${FLAVOUR}) already at $OUT"
		exit 0
	fi
	log "sha256 mismatch for existing $OUT, re-downloading"
fi

command -v curl >/dev/null 2>&1 || command -v wget >/dev/null 2>&1 || \
	die "need curl or wget to download busybox"

mkdir -p "$(dirname "$OUT")"
tmp="$OUT.part"
rm -f "$tmp"
log "downloading $URL"
if command -v curl >/dev/null 2>&1; then
	curl -fL --retry 3 -o "$tmp" "$URL"
else
	wget -O "$tmp" "$URL"
fi

have=$(sha "$tmp")
if [[ "$have" != "$SHA256" ]]; then
	rm -f "$tmp"
	die "sha256 mismatch for busybox: got $have want $SHA256"
fi

chmod +x "$tmp"
if command -v file >/dev/null 2>&1; then
	desc=$(file -b "$tmp")
	case "$desc" in
	*"ELF 64-bit LSB executable, x86-64"*"statically linked"*) ;;
	*) rm -f "$tmp"; die "not a static x86-64 ELF: $desc" ;;
	esac
	log "$desc"
fi
mv -f "$tmp" "$OUT"
log "wrote $OUT (busybox ${VERSION} ${FLAVOUR}, sha256 ${SHA256})"
