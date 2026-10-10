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
#
# Env: KOALAAMP_VENDOR_NATIVE=0 skips the native projectM fetch+build (the
# single-file deps still install). FE-14 builds a *shared* libprojectM into
# third_party/projectm/ for the QOpenGLWidget spike; the app links it
# dynamically, never statically (LGPL discipline).
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

# --- Native: projectM (LGPL-2.1-or-later; dynamic link only) ---------------
# FE-14. projectM is fetched as pinned source and built into a *shared* library
# under third_party/projectm/ (headers + lib + stamp). It is never committed
# and never statically linked into the app. projectM's equation compiler
# projectm-eval (MIT) is an upstream git submodule; GitHub's source tarballs
# omit it, so we fetch the exact commit projectM pins (see
# deps/dependencies.json) and extract it into vendor/projectm-eval.
#
# Pin: projectM 4.2.0-dev (master @ 349b7cc2). The latest *release* 4.1.8 is
# NOT sufficient to host the visualiser inside a QOpenGLWidget: its
# projectm_opengl_render_frame() hardcodes the draw framebuffer to 0, but
# QOpenGLWidget renders into its own FBO (defaultFramebufferObject()). The
# projectm_opengl_render_frame_fbo() entry point that takes a caller FBO only
# exists on master (API 4.2.0, unreleased). A freedesktop Qt spike proved 4.1.8
# produces an empty widget FBO; 4.2.0-dev renders. Move to 4.2.0 on release.
pm_version="4.2.0"
pm_rev="349b7cc2db312d47cef6be24c6856ced6643a385"
pm_eval_rev="22fb0cfd8f2dfbcd2b68f2443e7f44e19b32c09a"
pm_url="https://github.com/projectM-visualizer/projectm/archive/${pm_rev}.tar.gz"
pm_sha="6bf5d202ace891e1716874b877923c1567de8104f0b36c2544d4706c127a37ac"
pm_eval_url="https://github.com/projectM-visualizer/projectm-eval/archive/${pm_eval_rev}.tar.gz"
pm_eval_sha="48002253353392393a1a5a4d2b0fc04497cfff774f3a9df084ef5078aa4f28ee"
pm_src="${dest_root}/src/projectm-${pm_version}-${pm_rev:0:8}"
pm_eval_src="${pm_src}/vendor/projectm-eval"
pm_prefix="${dest_root}/projectm"
pm_build="${dest_root}/build/projectm-${pm_rev:0:8}"
pm_stamp="${pm_prefix}/.koalaamp-stamp"
pm_stamp_value="projectm ${pm_version}-dev ${pm_rev} eval ${pm_eval_rev}"

# Extract a verified tarball into dest (dest is replaced). strip leading dirs.
fetch_and_extract() { # <url> <sha256> <dest> <strip-components>
    local url="$1" want="$2" dest="$3" strip="$4" tmp got
    tmp="$(mktemp)"
    if ! curl -fsSL -o "${tmp}" "${url}"; then
        echo "ERROR: download failed: ${url}" >&2; rm -f "${tmp}"; return 1
    fi
    got="$(sha256_of "${tmp}")"
    if [[ "${got}" != "${want}" ]]; then
        echo "ERROR: hash mismatch for ${url}: want ${want}, got ${got}" >&2
        rm -f "${tmp}"; return 1
    fi
    rm -rf "${dest}"; mkdir -p "${dest}"
    tar -xzf "${tmp}" -C "${dest}" --strip-components="${strip}"
    rm -f "${tmp}"
}

projectm_ready() {
    [[ -f "${pm_stamp}" && "$(cat "${pm_stamp}")" == "${pm_stamp_value}" ]] \
        && [[ -e "${pm_prefix}/lib/libprojectM-4.so" ]]
}

if [[ "${KOALAAMP_VENDOR_NATIVE:-1}" == "0" ]]; then
    echo "skipping: projectM native build (KOALAAMP_VENDOR_NATIVE=0)"
elif [[ "${mode}" == "check" ]]; then
    if projectm_ready; then
        echo "up to date: projectM ${pm_version} (third_party/projectm)"
    else
        echo "MISSING or STALE: projectM ${pm_version} (run scripts/vendor-deps.sh)" >&2
        failed=1
    fi
elif projectm_ready; then
    echo "up to date: projectM ${pm_version} (third_party/projectm)"
else
    echo "fetching+building: projectM ${pm_version} (LGPL-2.1-or-later, shared)"
    if [[ ! -f "${pm_src}/CMakeLists.txt" ]]; then
        fetch_and_extract "${pm_url}" "${pm_sha}" "${pm_src}" 1 || failed=1
    fi
    if [[ "${failed}" -eq 0 && ! -f "${pm_eval_src}/CMakeLists.txt" ]]; then
        fetch_and_extract "${pm_eval_url}" "${pm_eval_sha}" "${pm_eval_src}" 1 || failed=1
    fi
    if [[ "${failed}" -eq 0 ]]; then
        mkdir -p "${pm_prefix}"
        generator=()
        if command -v ninja >/dev/null 2>&1; then generator=(-G Ninja); fi
        # Playlist/docs/tests off — we only need the shared core + GL renderer.
        if ! cmake -S "${pm_src}" -B "${pm_build}" "${generator[@]}" \
                -DCMAKE_BUILD_TYPE=Release \
                -DCMAKE_INSTALL_PREFIX="${pm_prefix}" \
                -DBUILD_SHARED_LIBS=ON \
                -DENABLE_PLAYLIST=OFF \
                -DENABLE_SYSTEM_PROJECTM_EVAL=OFF \
                -DBUILD_TESTING=OFF -DBUILD_DOCS=OFF -DENABLE_INSTALL=ON; then
            echo "ERROR: projectM configure failed — cmake, a C++ toolchain and" >&2
            echo "       OpenGL dev headers (e.g. libgl-dev) are required." >&2
            failed=1
        elif ! cmake --build "${pm_build}" --target install; then
            echo "ERROR: projectM build/install failed" >&2
            failed=1
        else
            printf '%s\n' "${pm_stamp_value}" > "${pm_stamp}"
            echo "installed: projectM ${pm_version} -> third_party/projectm"
        fi
    fi
fi

if [[ "${failed}" -ne 0 ]]; then
    echo "vendor-deps: FAILED" >&2
    exit 1
fi

echo "vendor-deps: ok (${mode})"
