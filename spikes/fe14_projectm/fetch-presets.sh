#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# fetch-presets.sh — download the FE-14 test corpus (10 MilkDrop presets) from
# the community "cream of the crop" pack at a PINNED commit into the gitignored
# third_party/ tree. Third-party preset files are never committed (AGENTS.md);
# they are fetched at test time.
#
# Usage: spikes/fe14_projectm/fetch-presets.sh [dest-dir]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
dest="${1:-${root}/third_party/presets/cream}"

# presets-cream-of-the-crop @ 2026-10 (commit pinned for reproducible fetching).
commit="0180df21f5e0bd39b9060cc5de420ed2f1f9e509"
base="https://raw.githubusercontent.com/projectM-visualizer/presets-cream-of-the-crop/${commit}"

# A deliberately varied set: fractal loops, lasers, serpent, glowsticks,
# geometric stripes, particles, supernova. Filenames contain no spaces.
presets=(
    "Fractal/Loops/phat_RippleNebula9.milk"
    "Fractal/Lattice/Che-slowmotion.milk"
    "Dancer/Lasers/376.milk"
    "Dancer/Serpent/milfography.milk"
    "Dancer/Glowsticks/285.milk"
    "Geometric/Stripes/Telek-goodoldcollingwoodforever.milk"
    "Particles/Points/220.milk"
    "Particles/Points/53.milk"
    "Supernova/Stars/305.milk"
    "Supernova/Radiate/BrainStain-sunrays.milk"
)

mkdir -p "${dest}"
index=0
for rel in "${presets[@]}"; do
    index=$((index + 1))
    out="${dest}/$(printf '%02d' "${index}")_$(basename "${rel}")"
    if [[ -s "${out}" ]]; then
        echo "up to date: $(basename "${out}")"
        continue
    fi
    curl -fsSL -o "${out}" "${base}/${rel}"
    echo "fetched: $(basename "${out}")"
done

echo "presets: ${#presets[@]} in ${dest} (cream-of-the-crop @ ${commit:0:8})"
