#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# render-and-diff.sh — reproduce the FE-3/SE-2 acceptance check end to end:
#   1. ensure the base-2.91.wsz fixture (gitignored, fetched on demand),
#   2. render our main window (spike_fe3) for the default and toggled states,
#   3. render Webamp's main window headlessly (reference/, needs node + Chrome),
#   4. pixel-diff both at 1x with a 0.1% mismatch budget,
#   5. then diff the whole SE-2 state matrix (SE-2 addition): every state the
#      capture harness can drive Webamp into is rendered from the *captured*
#      state JSON, so like is compared with like.
#
# SE-2 swap note: the renderer under test is `koalaamp::ui` (src/ui) — the code
# the product paints with. The spike's own MainWindowRenderer.cpp was deleted;
# the 0/31900 numbers below are produced by the app renderer.
#
# Usage: spikes/fe3_wsz/render-and-diff.sh [build-dir] [--core-only]
# Env:   CHROME_PATH  Chrome binary for the reference harness
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build="${root}/build/linux-gcc"
core_only=0
for arg in "$@"; do
    case "${arg}" in
        --core-only) core_only=1 ;;
        *) build="${arg}" ;;
    esac
done
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

if [[ "${core_only}" == "1" ]]; then
    echo "render-and-diff: artifacts in ${out}"
    exit "${status}"
fi

# --- SE-2 extended matrix --------------------------------------------------
# Each state is rendered from the JSON the capture harness read out of Webamp's
# live DOM, so the comparison uses the same status/volume/balance/position/
# shuffle/repeat/channels/time/kbps/khz/title/marquee offset/vis mode.
# Masking: none in general. The one exception is documented per state below.
for state in shade shade_play playing paused vol0 vol100 bal_left bal_right \
             pos_half pos_max playing_mono vis_off scroll; do
    "${spike}" --skin "${skin}" --from-json "${out}/reference-state.json" \
        --state-name "${state}" --out "${out}/ours_${state}.png" >/dev/null
    case "${state}" in
        vol0)
            # Webamp's own bug, not a rendering difference: its volume
            # background-position is `0 -${15*(round(28*0)-1)}px` = `0 --15px`,
            # invalid CSS, so the element keeps the previous frame (78) while
            # the thumb moves to 0. Real Winamp draws the strip's first frame,
            # which is what our clamped volumeFrameOffset does — so the volume
            # bar rect is excluded here instead of copying the bug.
            echo "  (vol0: volume bar rect masked — Webamp's invalid CSS keeps the old frame)"
            "${root}/spikes/fe3_wsz/diff.py" \
                --a "${out}/ours_${state}.png" --b "${out}/webamp_main_${state}.png" \
                --threshold 0.001 --mask 107,57,68,13 || status=1
            ;;
        *)
            "${root}/spikes/fe3_wsz/diff.py" \
                --a "${out}/ours_${state}.png" --b "${out}/webamp_main_${state}.png" \
                --threshold 0.001 || status=1
            ;;
    esac
done

echo "render-and-diff: artifacts in ${out}"
exit "${status}"
