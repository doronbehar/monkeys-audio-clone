#!/usr/bin/env bash
# Print the newest monkeys-audio version according to Repology.
#
# monkeysaudio.com has no release feed and no version control, but a handful of
# distributions track it closely, so Repology's aggregate "newest" marker is the
# earliest machine-readable signal that a new SDK zip went up.
set -euo pipefail

: "${REPOLOGY_USER_AGENT:=monkeys-audio-clone (+https://github.com/doronbehar/monkeys-audio-clone)}"
API=${REPOLOGY_API:-https://repology.org/api/v1/project/monkeys-audio}

json=$(curl -sSf -A "$REPOLOGY_USER_AGENT" "$API")

# Repology flags the highest version it has seen across all repositories as
# "newest"; several repositories usually carry it, hence the dedup.
version=$(jq -r '.[] | select(.status == "newest") | .version' <<<"$json" | sort -V | tail -1)

if [ -z "$version" ]; then
	echo "Repology lists no 'newest' monkeys-audio version:" >&2
	jq -r '.[] | [.repo, .version, (.status // "-")] | @tsv' <<<"$json" >&2
	exit 1
fi

printf '%s\n' "$version"
