# monkeys-audio-clone

An unofficial, machine-made git history of the [Monkey's Audio][ma] SDK.

Upstream publishes each release as a single `MAC_<version>_SDK.zip` on <https://monkeysaudio.com/files/>, announces it on [versionhistory.html][history], and keeps no version control, no tags and no archive of superseded zips.
That makes "what changed between 13.26 and 13.27?" a question nobody can answer a month later.
This repository answers it, with one commit per release and one `v<version>` tag per commit.

Each commit is the unpacked zip and nothing else, byte for byte: upstream's own file names, directory layout and contents, including the CRLF endings and the vendored 3rd-party blobs.
`.gitattributes` keeps git from rewriting any of it.
The only tracked files that are not upstream's are this README, `.gitattributes`, `tools/` and `.github/`.

The history starts at **9.20**.
Before that, the only tracked packaging of Monkey's Audio pointed at a deb-multimedia tarball of 3.99 rather than at an upstream SDK zip, so there is nothing to mirror.

```console
$ git tag --list 'v*' | sort -V | tail -3
$ git diff --stat v13.26 v13.27
$ git log --oneline -- Source/Shared/Version.h
```

## Where the history came from

**9.20 through 13.28** were recovered from the Nix binary cache, by `tools/import-from-nix-cache.sh`.
nixpkgs has bumped `monkeys-audio` on roughly every release since 2023 and fetches the SDK with `fetchzip`, a fixed-output derivation, so the store path of an unpacked release depends only on the recursive SHA-256 of its contents and never on the URL.
Every version Hydra ever built therefore remains addressable in `cache.nixos.org` as `<path-hash>.narinfo`, years after the zip itself stopped being served.
`tools/versions.tsv` holds the `(version, hash, nixpkgs commit)` triples, read back out of nixpkgs' git log by `tools/extract-nixpkgs-versions.sh`, and every downloaded NAR is checked against the hash nixpkgs pinned before it is committed.
Each of those commit messages records the store path, the SRI hash and the nixpkgs commit it came from, and is dated when nixpkgs picked the version up, typically within a week or two of the real release.

**Later versions** are imported from monkeysaudio.com directly, by `tools/import-upstream-zip.sh`, while the zip is still the current one.
Their commit messages record the zip's SHA-256.

The split is deliberate.
The Nix cache is a reliable archive of the past, but it only learns about a release once nixpkgs packages it; upstream serves the present and forgets the past.

The two paths agree.
Unpacking a zip straight from monkeysaudio.com reproduces the tree nixpkgs pinned, down to the permission bits a NAR records:

```console
$ unzip -q -X -d tree MAC_1343_SDK.zip
$ nix hash path --mode nar --algo sha256 tree
sha256-bZbWUPfWVODjuSg0nLp0+Ac7vmW2mfqeslL/AmQ3xzo=   # == nixpkgs' hash for 13.43
```

So a commit imported from upstream can be audited against nixpkgs later, once nixpkgs catches up with that version.

## Staying up to date

`.github/workflows/check-new-version.yml` runs once a day.
It asks [Repology's API][repology] for the newest `monkeys-audio` version any distribution carries, compares that against the highest `v*` tag here, and on a new version downloads the zip, commits it and pushes the tag.
If the download fails, most likely because the URL does not follow `MAC_<digits>_SDK.zip` for that release, it opens an issue instead of failing silently.

Repology is the trigger rather than the source of truth.
It aggregates fast-moving packagers such as deb-multimedia, FreeBSD and Homebrew, so it usually notices a release within a day, but it only ever tells us a version number.
The bytes always come from upstream.

The workflow also takes a `version` input, so a release it missed can be imported by hand from the Actions tab, or locally:

```console
$ tools/import-upstream-zip.sh 13.43
```

## Rebuilding the reconstructed history

```console
$ tools/extract-nixpkgs-versions.sh ~/src/nixpkgs > tools/versions.tsv
$ tools/import-from-nix-cache.sh            # all versions without a tag yet
$ tools/import-from-nix-cache.sh 13.27      # or just one
```

This needs `nix`, `curl`, `zstd` and `xz`.
Only `nix-store` and `nix-hash` are used, and nothing is built or added to the store.

## Not affiliated with Monkey's Audio

This is a third-party mirror.
Monkey's Audio is Matthew T. Ashland's work, and is covered by the licence in each release's `License.txt`.
Report bugs in the code itself to upstream, not here.

[ma]: https://monkeysaudio.com/
[history]: https://monkeysaudio.com/versionhistory.html
[repology]: https://repology.org/project/monkeys-audio/versions
