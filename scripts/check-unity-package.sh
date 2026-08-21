#!/usr/bin/env bash
#
# Refuses to let a broken Unity package leave the building.
#
# A package installed from a tarball or a git URL lands in an immutable folder,
# so Unity cannot repair anything there: a missing .meta means the asset is
# ignored and logged, and a malformed one means the asset is ignored quietly.
# Both are cheap to detect here and expensive to discover in a consumer project.
set -euo pipefail

DIR="${1:?usage: check-unity-package.sh <staged-package-dir>}"
cd "$DIR"

fail=0

# Everything Unity imports needs a .meta. Names starting with "." or ending with
# "~" are invisible to the AssetDatabase, so they neither need one nor can use
# one - that is what makes Samples~ and .gitkeep legitimate.
missing="$(find . -mindepth 1 \
	-not -name '*.meta' \
	-not -name '.*' -not -path '*/.*' \
	-not -path '*~*' \
	-exec sh -c '[ -e "$1.meta" ] || echo "${1#./}"' _ {} \;)"
if [ -n "$missing" ]; then
	echo "these would ship without a .meta:" >&2
	echo "$missing" | sort | sed 's/^/  /' >&2
	fail=1
fi

# The mirror image, and the reason this check exists for bcsv specifically: the
# plugin .meta files are committed while the natives they describe are built by
# CI and staged in. A .meta with no asset beside it means an artifact did not
# arrive, which otherwise ships as a package that imports cleanly and then fails
# at the first P/Invoke.
orphan="$(find . -name '*.meta' \
	-exec sh -c '[ -e "${1%.meta}" ] || echo "${1#./}"' _ {} \;)"
if [ -n "$orphan" ]; then
	echo "these .meta files have no asset beside them:" >&2
	echo "$orphan" | sort | sed 's/^/  /' >&2
	fail=1
fi

# Existing is not the same as usable. A .meta Unity cannot parse is ignored, and
# the asset it describes is ignored with it - which for an .asmdef means its
# scripts land in whatever assembly encloses them and fail against references
# they should have had. Only the two properties this shell can actually observe
# are asserted; CRLF is deliberately not checked, because grep strips CR before
# a test here would see it and the check would report clean whatever the bytes.
bad=""
while IFS= read -r meta; do
	[ -n "$meta" ] || continue
	grep -q "^guid: [0-9a-f]\{32\}$" "$meta" || bad="$bad${meta#./} (no guid line)
"
	if [ -n "$(tail -c 1 "$meta")" ]; then
		bad="$bad${meta#./} (no trailing newline)
"
	fi
done < <(find . -name '*.meta' | sort)
if [ -n "$bad" ]; then
	echo "malformed .meta files:" >&2
	printf '%s' "$bad" | sed 's/^/  /' >&2
	fail=1
fi

if [ "$fail" -ne 0 ]; then
	echo >&2
	echo "refusing to ship $DIR" >&2
	exit 1
fi

echo "package checks passed: $(find . -name '*.meta' | wc -l | tr -d ' ') .meta files, $(find . -type f | wc -l | tr -d ' ') files total"
