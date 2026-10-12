#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# render-and-diff.sh — reproduce the SE-5 acceptance check end to end:
#   1. ensure the base-2.91.wsz fixture (gitignored, fetched on demand),
#   2. render our equalizer window (spike_se5) for the golden EQ states,
#   3. render Webamp's equalizer window headlessly (reference/, needs node +
#      Chrome) from the same slider values,
#   4. pixel-diff both at 1x with a 0.1% mismatch budget.
#
# Usage: spikes/se5_eq/render-and-diff.sh [build-dir]
# Env:   CHROME_PATH  Chrome binary for the reference harness
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="${1:-${root}/build/linux-gcc}"
spike="${build}/spikes/se5_eq/spike_se5"
skin="${root}/third_party/skins/base-2.91.wsz"
refdir="${root}/spikes/se5_eq/reference"
out="$(mktemp -d)"

if [[ ! -x "${spike}" ]]; then
    echo "render-and-diff: build first: cmake --build --preset linux-gcc --target spike_se5" >&2
    exit 1
fi

"${root}/spikes/fe3_wsz/fetch-skin.sh"

states=(eq_default eq_on eq_bands_maxboost eq_bands_maxcut eq_preamp_maxboost eq_shade eq_shade_extremes)
for state in "${states[@]}"; do
    "${spike}" --skin "${skin}" --out "${out}/ours_${state}.png" --state "${state}"
done

if [[ ! -d "${refdir}/node_modules" ]]; then
    echo "render-and-diff: installing reference harness deps (puppeteer-core)"
    (cd "${refdir}" && npm install --no-audit --no-fund)
fi
(cd "${refdir}" && node capture.mjs "${skin}" "${out}" \
    "${root}/third_party/webamp/webamp.bundle.min.js")

status=0
for state in "${states[@]}"; do
    "${root}/spikes/fe3_wsz/diff.py" \
        --a "${out}/ours_${state}.png" --b "${out}/webamp_${state}.png" \
        --threshold 0.001 || status=1
done

echo "render-and-diff: artifacts in ${out}"
exit "${status}"
