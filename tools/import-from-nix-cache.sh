#!/usr/bin/env bash
# Rebuild the historical part of the mirror out of the Nix binary cache.
#
# Usage: tools/import-from-nix-cache.sh [version ...]
#
# nixpkgs fetches the SDK with fetchzip, a fixed-output derivation, so its store
# path depends only on the recursive sha256 of the unpacked tree and the output
# name "source" -- not on the URL. Every version Hydra ever built therefore
# stays addressable in cache.nixos.org as <path-hash>.narinfo, years after
# monkeysaudio.com stopped serving that zip. tools/versions.tsv holds the
# (version, hash, nixpkgs commit) triples read back out of nixpkgs' git history.
#
# Needs nix (for nix-store/nix-hash only, nothing is built), curl, jq-free.
set -euo pipefail

# shellcheck source=tools/common.sh
. "$(dirname "$0")/common.sh"

CACHE=${CACHE:-https://cache.nixos.org}
root=$(repo_root)
work=${WORK_DIR:-$(mktemp -d)}
mkdir -p "$work"

wanted=("$@")
want() {
	if [ ${#wanted[@]} -eq 0 ]; then
		return 0
	fi
	local v
	for v in "${wanted[@]}"; do
		if [ "$v" = "$1" ]; then
			return 0
		fi
	done
	return 1
}

while IFS=$'\t' read -r version hash date commit; do
	case $version in '' | \#*) continue ;; esac
	want "$version" || continue
	check_version_format "$version"

	if git -C "$root" rev-parse -q --verify "refs/tags/v$version" >/dev/null; then
		echo "== v$version already in the mirror"
		continue
	fi

	store_path=$(nix-store --print-fixed-path --recursive sha256 "$hash" source)
	path_hash=${store_path#/nix/store/}
	path_hash=${path_hash%%-*}

	narinfo=$work/$version.narinfo
	if [ ! -s "$narinfo" ]; then
		echo "== $version: fetching $path_hash.narinfo"
		curl -sSf -o "$narinfo" "$CACHE/$path_hash.narinfo"
	fi

	nar_url=$(sed -n 's/^URL: //p' "$narinfo")
	compression=$(sed -n 's/^Compression: //p' "$narinfo")
	nar=$work/$version.nar
	if [ ! -s "$nar" ]; then
		echo "== $version: fetching $nar_url ($compression)"
		case $compression in
		zstd) curl -sSfL "$CACHE/$nar_url" | zstd -d >"$nar" ;;
		xz) curl -sSfL "$CACHE/$nar_url" | xz -d >"$nar" ;;
		none) curl -sSfL -o "$nar" "$CACHE/$nar_url" ;;
		*)
			echo "$version: unhandled narinfo compression '$compression'" >&2
			exit 1
			;;
		esac
	fi

	# A fetchzip hash is the recursive (NAR) hash of the unpacked tree, and the
	# NAR serialisation of a directory is exactly the bytes of this file -- so
	# the flat hash of the .nar must equal the hash nixpkgs pinned. Checking it
	# keeps a tampered cache or a truncated download out of the mirror.
	want_hash=$(nix hash convert --hash-algo sha256 --to nix32 "$hash")
	got_hash=$(nix hash convert --hash-algo sha256 --to nix32 \
		"sha256:$(nix-hash --type sha256 --flat "$nar")")
	if [ "$got_hash" != "$want_hash" ]; then
		echo "$version: NAR hashes to $got_hash, nixpkgs pinned $want_hash" >&2
		exit 1
	fi

	echo "== $version: unpacking and committing"
	rm -rf "$work/tree"
	nix-store --restore "$work/tree" <"$nar"

	replace_tree "$work/tree"
	commit_version "$version" "Nix-Store-Path: $store_path
Nix-Sri-Hash: $hash
Nixpkgs-Commit: https://github.com/NixOS/nixpkgs/commit/$commit
Imported-By: tools/import-from-nix-cache.sh" "$date 12:00:00 +0000"
done <"$root/tools/versions.tsv"
