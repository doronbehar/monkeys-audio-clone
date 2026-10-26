#!/usr/bin/env bash
# Regenerate tools/versions.tsv from a nixpkgs checkout.
#
# Usage: tools/extract-nixpkgs-versions.sh <path-to-nixpkgs> > tools/versions.tsv
#
# Walks every commit that touched the monkeys-audio expression, at both paths it
# has lived under, and prints each distinct (version, src hash) pair with the
# commit that introduced it. Versions before 9.20 are skipped: back then nixpkgs
# packaged a deb-multimedia tarball of 3.99, not a monkeysaudio.com SDK zip.
set -euo pipefail

if [ $# -ne 1 ]; then
	echo "usage: $0 <path-to-nixpkgs>" >&2
	exit 2
fi
nixpkgs=$1

BY_NAME=pkgs/by-name/mo/monkeys-audio/package.nix
LEGACY=pkgs/applications/audio/monkeys-audio/default.nix

# --follow across the rename is far too slow on nixpkgs, so list both paths and
# rely on the legacy one being strictly older.
{
	git -C "$nixpkgs" log --reverse --format="%H %ad $LEGACY" --date=short -- "$LEGACY"
	git -C "$nixpkgs" log --reverse --format="%H %ad $BY_NAME" --date=short -- "$BY_NAME"
} | while read -r commit date path; do
	blob=$(git -C "$nixpkgs" show "$commit:$path")
	version=$(sed -n 's/^[[:space:]]*version = "\([^"]*\)";.*/\1/p' <<<"$blob" | head -1)
	hash=$(sed -n 's/^[[:space:]]*\(hash\|sha256\) = "\([^"]*\)";.*/\2/p' <<<"$blob" | head -1)
	if [ -z "$version" ] || [ -z "$hash" ]; then
		echo "$commit:$path has no version/hash pair" >&2
		exit 1
	fi
	case $version in
	3.*) continue ;;
	esac
	printf '%s\t%s\t%s\t%s\n' "$version" "$hash" "$date" "$commit"
done | awk -F'\t' '!seen[$1]++'
