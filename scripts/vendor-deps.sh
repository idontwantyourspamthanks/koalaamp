#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# vendor-deps.sh — fetch the pinned single-file dependencies into the
# gitignored third_party/ tree. Idempotent and re-runnable: a second run
# reports "up to date" and touches nothing.
#
# Scope: the single-file dependencies (miniaudio, nlohmann/json) are fetched
# into the gitignored third_party/ tree; the *native* libraries (Qt, libvlc,
# TagLib, projectM, SQLite, libcurl) are acquired per platform (system packages
# / vcpkg / prebuilt-Qt) as recorded in docs/spikes/fe0-deps.md and enumerated
# in deps/dependencies.json. FE-2 added the native section below for libvlc:
# on Linux the pinned version comes from the distro dev package (installed with
# sudo when missing, verified when present); off Linux the vcpkg/Homebrew paths
# from the table apply. Native libs stay separate files — never vendored into
# the tree, never single-file-merged, never statically linked into the app
# binary (LGPL discipline, docs/feasibility.md §6).
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

# --- native dependencies (FE-2) --------------------------------------------
# name|package|probe command|install command. The probe prints the pinned
# version; a non-empty result means the dependency is usable. On Linux libvlc
# is a distro dev package; the build finds it via pkg-config (CMake). Off Linux
# these are acquired through vcpkg (Windows) / Homebrew (macOS) per
# docs/spikes/fe0-deps.md and are not handled here.
native_linux=(
    "libvlc|libvlc-dev|pkg-config --modversion libvlc|sudo apt-get install -y libvlc-dev"
)

if [[ "$(uname -s)" == "Linux" ]]; then
    for spec in "${native_linux[@]}"; do
        IFS='|' read -r name pkg probe install_cmd <<<"${spec}"
        have="$(eval "${probe}" 2>/dev/null || true)"
        if [[ -n "${have}" ]]; then
            echo "present: ${name} ${have} (${pkg})"
            continue
        fi
        if [[ "${mode}" == "check" ]]; then
            echo "MISSING: ${name} (${pkg}); install with: ${install_cmd}" >&2
            failed=1
            continue
        fi
        echo "installing: ${name} (${install_cmd})"
        if ! eval "${install_cmd}"; then
            echo "ERROR: could not install ${name}; run manually: ${install_cmd}" >&2
            failed=1
            continue
        fi
        echo "installed: ${name} $(eval "${probe}" 2>/dev/null || echo '?')"
    done
else
    echo "note: native deps (libvlc, TagLib, projectM) use vcpkg/Homebrew off Linux — see docs/spikes/fe0-deps.md"
fi

if [[ "${failed}" -ne 0 ]]; then
    echo "vendor-deps: FAILED" >&2
    exit 1
fi

echo "vendor-deps: ok (${mode})"
