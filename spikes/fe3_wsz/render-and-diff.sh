#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# render-and-diff.sh — reproduce the FE-3 acceptance check end to end:
#   1. ensure the base-2.91.wsz fixture (gitignored, fetched on demand),
#   2. render our main window (spike_fe3) for the default and toggled states,
#   3. render Webamp's main window headlessly (reference/, needs node + Chrome),
#   4. pixel-diff both at 1x with a 0.1% mismatch budget.
#
# Usage: spikes/fe3_wsz/render-and-diff.sh [build-dir]
# Env:   CHROME_PATH  Chrome binary for the reference harness
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="${1:-${root}/build/linux-gcc}"
spike="${build}/spikes/fe3_wsz/spike_fe3"
skin="${root}/third_party/skins/base-2.91.wsz"
refdir="${root}/spikes/fe3_wsz/reference"
out="$(mktemp -d)"

if [[ ! -x "${spike}" ]]; then
    echo "render-and-diff: build first: cmake --build --preset linux-gcc --target spike_fe3" >&2
    exit 1
fi

"${root}/spikes/fe3_wsz/fetch-skin.sh"

"${spike}" --skin "${skin}" --out "${out}/ours_default.png" --state default
"${spike}" --skin "${skin}" --out "${out}/ours_toggled.png" --state toggled

# Reference renders (Webamp in headless Chrome).
if [[ ! -d "${refdir}/node_modules" ]]; then
    echo "render-and-diff: installing reference harness deps (puppeteer-core)"
    (cd "${refdir}" && npm install --no-audit --no-fund)
fi
(cd "${refdir}" && node capture.mjs "${skin}" "${out}" "${root}/third_party/webamp/webamp.bundle.min.js")

status=0
"${root}/spikes/fe3_wsz/diff.py" \
    --a "${out}/ours_default.png" --b "${out}/webamp_main_default.png" \
    --threshold 0.001 || status=1
"${root}/spikes/fe3_wsz/diff.py" \
    --a "${out}/ours_toggled.png" --b "${out}/webamp_main_toggled.png" \
    --threshold 0.001 || status=1

echo "render-and-diff: artifacts in ${out}"
exit "${status}"
