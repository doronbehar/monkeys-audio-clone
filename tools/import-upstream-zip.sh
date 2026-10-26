#!/usr/bin/env bash
# Import one version straight from monkeysaudio.com, as a commit plus a v<version> tag.
#
# Usage: tools/import-upstream-zip.sh <version>
#
# This is how new versions enter the mirror: upstream only keeps the current
# MAC_*_SDK.zip reachable for a while, so the zip has to be grabbed while it is
# still the current one. Older versions came in via import-from-nix-cache.sh.
set -euo pipefail

# shellcheck source=tools/common.sh
. "$(dirname "$0")/common.sh"

if [ $# -ne 1 ]; then
	echo "usage: $0 <version>" >&2
	exit 2
fi
version=$1
check_version_format "$version"

root=$(repo_root)
url=$(sdk_url "$version")

if git -C "$root" rev-parse -q --verify "refs/tags/v$version" >/dev/null; then
	echo "v$version is already in the mirror" >&2
	exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

echo "== downloading $url"
curl -sSfL -o "$work/sdk.zip" "$url"
sha256=$(sha256sum "$work/sdk.zip" | cut -d' ' -f1)
echo "== sha256:$sha256"

# -X drops the extra attributes unzip would otherwise try to restore; the mirror
# only carries names, bytes and the directory layout.
mkdir "$work/tree"
unzip -q -X -d "$work/tree" "$work/sdk.zip"

replace_tree "$work/tree"
commit_version "$version" "Zip-Sha256: sha256:$sha256
Imported-By: tools/import-upstream-zip.sh"

echo "== committed and tagged v$version"
