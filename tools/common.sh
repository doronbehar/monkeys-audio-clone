# Shared helpers for the monkeys-audio-clone import scripts.
# Sourced, not executed.
# shellcheck shell=bash

# Everything in the repository root that belongs to the mirror itself rather
# than to an upstream MAC_*_SDK.zip. A version import wipes the rest.
MIRROR_PATHS=(.git .github .gitattributes README.md tools)

repo_root() {
	git -C "$(dirname "${BASH_SOURCE[0]}")" rev-parse --show-toplevel
}

# Upstream drops the dots: 13.28 -> MAC_1328_SDK.zip.
sdk_url() {
	local version=$1
	printf 'https://monkeysaudio.com/files/MAC_%s_SDK.zip\n' "${version//./}"
}

# Upstream numbers releases <major>.<two digits>. Anything else means either a
# typo or that upstream changed their scheme, and in both cases guessing a zip
# URL from it is wrong, so refuse instead.
check_version_format() {
	local version=$1
	if ! [[ $version =~ ^[0-9]+\.[0-9][0-9]$ ]]; then
		echo "'$version' is not a <major>.<two digits> monkeys-audio version" >&2
		return 1
	fi
}

version_le() {
	[ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -1)" = "$1" ]
}

# Replace the tracked source tree with the contents of $1, leaving MIRROR_PATHS
# alone. $1 must be the directory the SDK zip unpacks into, i.e. the one holding
# CMakeLists.txt.
replace_tree() {
	local src=$1 root p
	root=$(repo_root)

	if [ ! -f "$src/CMakeLists.txt" ]; then
		echo "$src does not look like an unpacked MAC SDK (no CMakeLists.txt)" >&2
		return 1
	fi
	# The wipe below would eat the source tree along with the old version.
	case $(realpath "$src") in
	"$root" | "$root"/*)
		echo "$src is inside the repository; unpack somewhere else" >&2
		return 1
		;;
	esac
	for p in "${MIRROR_PATHS[@]}"; do
		if [ -e "$src/$p" ]; then
			echo "$src contains '$p', which would clobber the mirror's own files" >&2
			return 1
		fi
	done

	local prune=()
	for p in "${MIRROR_PATHS[@]}"; do
		prune+=(-name "$p" -prune -o)
	done
	find "$root" -mindepth 1 -maxdepth 1 "${prune[@]}" -print0 |
		xargs -0 --no-run-if-empty rm -rf --

	cp -a "$src"/. "$root"/
	# Nix store paths and unzip -X both hand back read-only trees.
	chmod -R u+w "$root"
}

# Commit the current tree as $version and tag it v$version. $2 is appended to
# the commit message as provenance trailers; $3, if given, is the commit date.
commit_version() {
	local version=$1 trailers=$2 date=${3:-} root url
	root=$(repo_root)
	url=$(sdk_url "$version")

	git -C "$root" add -A
	if git -C "$root" diff --cached --quiet; then
		echo "!! $version is byte-identical to $(git -C "$root" describe --tags --abbrev=0)," \
			"tagging without a commit" >&2
	else
		(
			if [ -n "$date" ]; then
				export GIT_AUTHOR_DATE=$date GIT_COMMITTER_DATE=$date
			fi
			git -C "$root" commit --quiet --no-verify \
				-m "$version" \
				-m "Imported from $url" \
				-m "$trailers"
		)
	fi
	git -C "$root" tag "v$version"
}
