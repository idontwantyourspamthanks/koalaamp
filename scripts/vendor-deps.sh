#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# vendor-deps.sh — fetch the pinned single-file dependencies into the
# gitignored third_party/ tree. Idempotent and re-runnable: a second run
# reports "up to date" and touches nothing.
#
# Scope: only the dependencies that vendor cleanly as single files
# (miniaudio, nlohmann/json). The native libraries — Qt, libvlc, TagLib,
# projectM, SQLite, libcurl — are acquired per platform (system packages /
# vcpkg / prebuilt-Qt) as recorded in docs/spikes/fe0-deps.md and enumerated
# in deps/dependencies.json. When their integrations land (FE-2/FE-5/FE-14)
# their pinned sources are added here; native libs stay separate files — never
# single-file-merged, never statically linked into the app binary (LGPL
# discipline, docs/feasibility.md §6).
#
# Usage: scripts/vendor-deps.sh [--check | --clean]
#   --check  verify everything is present and hash-correct; change nothing
#   --clean  remove third_party/ entirely
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest_root="${root}/third_party"
mode="install"

if [[ "${1:-}" == "--check" ]]; then
    mode="check"
elif [[ "${1:-}" == "--clean" ]]; then
    if [[ -d "${dest_root}" ]]; then
        find "${dest_root}" -mindepth 1 ! -name README.md -exec rm -rf {} +
    fi
    echo "vendor-deps: cleaned ${dest_root} (kept README.md)"
    exit 0
fi

# name|version|url|sha256|destination (relative to third_party/)
deps=(
    "miniaudio|0.11.22|https://raw.githubusercontent.com/mackron/miniaudio/0.11.22/miniaudio.h|9019743287e443c55e5737a7297f38e5e358561701d6db2d905afb114390c410|include/miniaudio.h"
    "nlohmann-json|3.11.3|https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp|9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6|include/nlohmann/json.hpp"
)

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    else
        shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

failed=0
for spec in "${deps[@]}"; do
    IFS='|' read -r name version url want rel <<<"${spec}"
    out="${dest_root}/${rel}"

    if [[ -f "${out}" ]] && [[ "$(sha256_of "${out}")" == "${want}" ]]; then
        echo "up to date: ${name} ${version} (${rel})"
        continue
    fi

    if [[ "${mode}" == "check" ]]; then
        echo "MISSING or STALE: ${name} ${version} (${rel})" >&2
        failed=1
        continue
    fi

    echo "fetching: ${name} ${version}"
    tmp="$(mktemp)"
    if ! curl -fsSL -o "${tmp}" "${url}"; then
        echo "ERROR: download failed: ${url}" >&2
        rm -f "${tmp}"
        failed=1
        continue
    fi

    got="$(sha256_of "${tmp}")"
    if [[ "${got}" != "${want}" ]]; then
        echo "ERROR: hash mismatch for ${name}: want ${want}, got ${got}" >&2
        rm -f "${tmp}"
        failed=1
        continue
    fi

    mkdir -p "$(dirname "${out}")"
    mv "${tmp}" "${out}"
    echo "installed: ${name} ${version} -> ${rel}"
done

if [[ "${failed}" -ne 0 ]]; then
    echo "vendor-deps: FAILED" >&2
    exit 1
fi

echo "vendor-deps: ok (${mode})"
