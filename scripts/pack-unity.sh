#!/usr/bin/env bash
#
# Stages the shippable contents of unity/ into a directory.
#
# Both delivery paths use this: the .tgz workflow stages into <staging>/package
# and tars it, the upm-branch workflow stages into the branch working tree. What
# a consumer receives is therefore the same set of files either way.
#
# Natives and the version stamp are not this script's business - CI stages the
# built libraries and patches package.json afterwards. Run check-unity-package.sh
# on the result once both have happened.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="${1:?usage: pack-unity.sh <staging-dir>}"

mkdir -p "$DEST"

# Ship what a consumer installs and leave repository plumbing behind. This is an
# allowlist rather than a copy of unity/* because the failure mode of copying
# runs the wrong way: a file added under unity/ reaches consumers by default and
# nothing says so. That is how tools/build-windows.ps1 came to ship, warning
# "has no meta file, but it's in an immutable folder" on every domain reload for
# everyone who installed the package.
# Tests/ ships deliberately: its .asmdef carries defineConstraints
# UNITY_INCLUDE_TESTS, so the assembly is skipped entirely unless a consumer
# opts the package into their testables. Leaving it out would mean the
# package could never be verified against an actual installation.
for item in package.json Runtime Tests Samples~ README.md CHANGELOG.md LICENSE.md; do
	[ -e "$ROOT/unity/$item" ] || continue
	cp -r "$ROOT/unity/$item" "$DEST/"
	# Sidecar .meta travels with the asset. Names ending in "~" are invisible to
	# Unity and have none, hence the test rather than an unconditional copy.
	if [ -e "$ROOT/unity/$item.meta" ]; then
		cp "$ROOT/unity/$item.meta" "$DEST/"
	fi
done

# The package's licence is the repository's, copied under the name UPM expects.
# Its committed .meta is already in place from the loop above.
cp "$ROOT/LICENSE" "$DEST/LICENSE.md"

echo "staged unity package into $DEST"
