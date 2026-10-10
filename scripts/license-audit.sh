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

[ "$fail" -eq 0 ] && echo "license-audit: clean"
exit $fail
