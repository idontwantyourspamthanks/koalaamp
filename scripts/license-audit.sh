#!/usr/bin/env bash
# license-audit — deny GPL/AGPL in direct references (LGPL is explicitly allowed:
# our stack legitimately links Qt, libvlc, projectM, TagLib under LGPL dynamically).
# Extended by FE-0 to parse the dependency manifest; today it guards the
# docs' licensing promises and greps CMake for forbidden links.
set -euo pipefail

cd "$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

fail=0

# GPL/AGPL, but never LGPL/MPL: requires a word boundary that is not 'L'.
#  - " GPL-3.0" / "GPLv3" / "AGPL"     -> FAIL
#  - "LGPL-2.1" / "MPL-2.0" / "lgpl"   -> pass
grep_gpl() { grep -qniE "(^|[^a-z])((gpl|agpl)([- v]?[0-9.]+)?)" "$1" 2>/dev/null; }

# 1) No GPL/AGPL in build wiring (docs/research citations are allowed —
#    the research reports *document* GPL projects we deliberately avoid).
for f in CMakeLists.txt $(find src tests scripts -name "CMakeLists.txt" -o -name "*.cmake" 2>/dev/null); do
  [ -f "$f" ] || continue
  if grep_gpl "$f"; then
    echo "ERROR: copyleft reference in $f" >&2
    fail=1
  fi
done

# 2) Source headers must not claim GPL inheritance.
for f in $(find src -name "*.cpp" -o -name "*.h" 2>/dev/null); do
  [ -f "$f" ] || continue
  if grep_gpl "$f"; then
    echo "ERROR: GPL-licensed file in src/: $f" >&2
    fail=1
  fi
done

# 3) MIT adaptations must carry attribution (checked when they exist).
for f in $(git grep -l "adapted from Webamp\|adapted from webamp" -- "src/*.cpp" "src/*.h" 2>/dev/null || true); do
  if ! grep -qi "copyright.*jordan\|MIT" "$f"; then
    echo "WARN: possible unattributed Webamp adaptation: $f" >&2
  fi
done

# 4) Jellyfin header-auth rule (spec §8.2): no legacy auth in server code.
for f in $(find src/servers -type f 2>/dev/null || true); do
  if grep -qniE "X-Emby-Token|api_key=" "$f"; then
    echo "ERROR: deprecated Jellyfin auth in $f" >&2
    fail=1
  fi
done

# 5) Dependency manifest — turn the license inventory into enforceable fact
#    (FE-0). Every dependency must carry an allowed license; LGPL components
#    must be dynamically linked; the required inventory must be complete.
manifest="deps/dependencies.json"
if [ -f "$manifest" ]; then
  if python3 - "$manifest" <<'PY'
import json
import re
import sys

path = sys.argv[1]
with open(path, encoding="utf-8") as fh:
    data = json.load(fh)

deps = data.get("dependencies", [])
policy = data.get("policy", {})
allowed = [a.lower() for a in policy.get("allowed_non_lgpl_licenses", [])]
lgpl_link = policy.get("lgpl_link_mode", "dynamic")

# Deny GPL/AGPL but never LGPL (the lookbehind excludes a preceding letter).
denied = re.compile(r"(?<![A-Za-z])A?GPL", re.IGNORECASE)
required = {"Qt", "libvlc", "TagLib", "projectM", "SQLite",
            "miniaudio", "libcurl", "JSON", "Catch2"}

errors = []
for dep in deps:
    name = dep.get("name", "<unnamed>")
    lic = dep.get("license", "")
    link = dep.get("link", "")
    if denied.search(lic):
        errors.append(f"manifest: denied license {lic!r} on {name}")
    if "lgpl" in lic.lower():
        if link != lgpl_link:
            errors.append(
                f"manifest: {name} is LGPL but link mode is {link!r} "
                f"(must be {lgpl_link!r} — dynamic linking only)")
    elif not any(token in lic.lower() for token in allowed):
        errors.append(f"manifest: {name} has unrecognized license {lic!r}")

present = {d.get("name") for d in deps}
for missing in sorted(required - present):
    errors.append(f"manifest: required dependency missing: {missing}")

if errors:
    for err in errors:
        print(err, file=sys.stderr)
    sys.exit(1)

print(f"dependency manifest ok ({len(deps)} deps)")
PY
  then
    :
  else
    fail=1
  fi
else
  echo "ERROR: missing dependency manifest: $manifest" >&2
  fail=1
fi

[ "$fail" -eq 0 ] && echo "license-audit: clean"
exit $fail
