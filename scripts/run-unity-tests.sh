#!/usr/bin/env bash
#
# Runs the package's EditMode tests in a headless editor.
#
# WHY THIS EXISTS RATHER THAN A ONE-LINE Unity INVOCATION
#
# Unity's AssetDatabase deletes any .meta file it finds with no asset beside it.
# unity/Runtime/Plugins/ is full of exactly that: the four native sidecars are
# committed while the binaries they describe are built by CI and injected at
# pack time, which is the arrangement check-unity-package.sh exists to protect.
# So pointing an editor straight at unity/ as a local package silently deletes
# three committed .meta files -- and the next release then ships plugins Unity
# ignores, which is a runtime P/Invoke failure in a consumer's project rather
# than a build error in ours.  This happened during 1.5.17 development.
#
# Placeholder binaries are staged first so nothing is orphaned.  They are
# gitignored, and the real Linux one is used where it exists so the tests can
# actually call into the library.
set -euo pipefail

EDITOR="${1:?usage: run-unity-tests.sh <path-to-Unity-executable> [build-dir]}"
BUILD="${2:-build}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PKG="$ROOT/unity"
PROJ="$(mktemp -d)"
trap 'rm -rf "$PROJ"' EXIT

# ── Keep the AssetDatabase from tidying away the committed sidecars ──────
staged=()
stage() {
	[ -e "$1" ] && return 0
	mkdir -p "$(dirname "$1")"
	printf 'placeholder' > "$1"
	staged+=("$1")
}
stage "$PKG/Runtime/Plugins/Windows/x86_64/bcsv_c_api.dll"
stage "$PKG/Runtime/Plugins/Linux/arm64/libbcsv_c_api.so"
stage "$PKG/Runtime/Plugins/macOS/libbcsv_c_api.dylib"

# The one the tests actually load: use the real build if it is there.
if [ -f "$ROOT/$BUILD/libbcsv_c_api.so" ]; then
	mkdir -p "$PKG/Runtime/Plugins/Linux/x86_64"
	cp "$ROOT/$BUILD/libbcsv_c_api.so" "$PKG/Runtime/Plugins/Linux/x86_64/"
else
	stage "$PKG/Runtime/Plugins/Linux/x86_64/libbcsv_c_api.so"
	echo "warning: no $BUILD/libbcsv_c_api.so — tests that call the library will fail" >&2
fi

mkdir -p "$PROJ/Packages" "$PROJ/Assets" "$PROJ/ProjectSettings"
cat > "$PROJ/Packages/manifest.json" <<JSON
{
  "dependencies": {
    "com.bcsv.unity": "file:$PKG",
    "com.unity.test-framework": "1.4.5"
  },
  "testables": [ "com.bcsv.unity" ]
}
JSON
"$EDITOR" -version > /dev/null 2>&1 || true
echo "m_EditorVersion: $("$EDITOR" -version 2>/dev/null | head -1 || echo 6000.0.0f1)" \
	> "$PROJ/ProjectSettings/ProjectVersion.txt"

set +e
"$EDITOR" -batchmode -nographics -projectPath "$PROJ" \
	-runTests -testPlatform EditMode \
	-testResults "$PROJ/results.xml" -logFile "$PROJ/unity.log"
status=$?
set -e

if [ ! -f "$PROJ/results.xml" ]; then
	echo "no test results produced; last errors from the editor log:" >&2
	grep -E "error CS|Aborting|Cannot find module" "$PROJ/unity.log" | head -10 >&2 || true
	exit 1
fi

python3 - "$PROJ/results.xml" <<'PY'
import sys, xml.etree.ElementTree as ET
r = ET.parse(sys.argv[1]).getroot()
print("EditMode: total=%s passed=%s failed=%s skipped=%s"
      % (r.get("total"), r.get("passed"), r.get("failed"), r.get("skipped")))
bad = [tc for tc in r.iter("test-case") if tc.get("result") != "Passed"]
for tc in bad:
    print("  %s %s" % (tc.get("result"), tc.get("fullname")))
sys.exit(1 if bad else 0)
PY
rc=$?

# Leave the tree as it was found.
for f in "${staged[@]:-}"; do [ -n "$f" ] && rm -f "$f"; done

exit $rc
