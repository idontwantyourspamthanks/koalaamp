#!/bin/sh
# SPDX-License-Identifier: MIT
#
# KoalaAmp launcher: builds, then runs the app.
#
#   ./run.sh                          build (no-op when up to date), then run
#   ./run.sh --skin path/to/skin.wsz  any .wsz/.zip, or an extracted skin folder
#   ./run.sh --null-audio             run silent (no audio device)
#   ./run.sh --always-on-top          keep the window set above other windows
#   ./run.sh --self-test              construct the window set, map, exit 0
#   ./run.sh --no-build               skip the build, run the existing binary
#   ./run.sh --build                  also re-run the configure step
#
# Anything else goes straight to the app. ENV:
#   PRESET=<cmake preset>   build preset (default: linux-gcc; linux-clang when
#                           g++ is missing; macos-clang on macOS)
#   QT_QPA_PLATFORM=offscreen   headless run (combine with --self-test)

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$root"

usage() {
    cat <<'EOF'
KoalaAmp launcher: builds, then runs the app.

  ./run.sh                          build (no-op when up to date), then run
  ./run.sh --skin path/to/skin.wsz  any .wsz/.zip, or an extracted skin folder
  ./run.sh --null-audio             run silent (no audio device)
  ./run.sh --always-on-top          keep the window set above other windows
  ./run.sh --self-test              construct the window set, map, exit 0
  ./run.sh --no-build               skip the build, run the existing binary
  ./run.sh --build                  also re-run the configure step

Without --skin the app restores the skin you last used; if there is none yet,
the launcher fetches the classic base skin (third_party/ is not committed).

Anything else goes straight to the app. ENV:
  PRESET=<cmake preset>   build preset (default: linux-gcc; linux-clang when
                          g++ is missing; macos-clang on macOS)
  QT_QPA_PLATFORM=offscreen   headless run (combine with --self-test)

The skinned windows are frameless and OS-masked, and the compositor places the
player, EQ and playlist windows — so they can start on top of one another: drag
them apart. Snapping then keeps the group together, and the arrangement is
remembered per skin. More skins: scripts/fetch-corpus.sh (30 of them).
EOF
}

# --- build preset ------------------------------------------------------------

if [ -n "${PRESET:-}" ]; then
    preset=$PRESET
elif [ "$(uname -s)" = "Darwin" ]; then
    preset=macos-clang
elif command -v g++ >/dev/null 2>&1; then
    preset=linux-gcc
else
    preset=linux-clang
fi

app=build/$preset/src/koalaamp
if [ ! -x "$app" ] && [ -x "$app.exe" ]; then
    app=$app.exe
fi

# --- arguments: our flags are consumed, the rest goes to the app -------------

skip_build=0
force_configure=0
have_skin=0
n=$#
i=0
while [ "$i" -lt "$n" ]; do
    arg=$1
    shift
    case $arg in
        -h|--help)
            usage
            exit 0
            ;;
        --no-build)
            skip_build=1
            ;;
        --build)
            force_configure=1
            ;;
        --skin|-s|--skin=*)
            have_skin=1
            set -- "$@" "$arg"
            ;;
        *)
            set -- "$@" "$arg"
            ;;
    esac
    i=$((i + 1))
done

# --- default skin ------------------------------------------------------------
# Order: the user's --skin, else the skin the app remembers from last time (it
# restores that itself, so passing one here would override their choice), else
# the classic base skin -- fetched on demand, because third_party/ is gitignored
# and a fresh clone has no skins at all.

if [ "$have_skin" -eq 0 ]; then
    base_skin=third_party/skins/base-2.91.wsz
    config_dir=${XDG_CONFIG_HOME:-$HOME/.config}/KoalaAmp/KoalaAmp
    if [ -f "$base_skin" ]; then
        set -- "$@" --skin "$base_skin"
    elif grep -qs '^current' "$config_dir/skindb.txt"; then
        : # the app restores the remembered skin itself
    elif [ -x spikes/fe3_wsz/fetch-skin.sh ]; then
        echo "run.sh: fetching the classic base skin (third_party/ is not committed)"
        spikes/fe3_wsz/fetch-skin.sh || echo "run.sh: fetch failed — pass --skin <file.wsz>" >&2
        if [ -f "$base_skin" ]; then
            set -- "$@" --skin "$base_skin"
        fi
    fi
fi

# --- build -------------------------------------------------------------------

if [ "$skip_build" -eq 0 ]; then
    if ! command -v cmake >/dev/null 2>&1; then
        echo "run.sh: cmake not found (needs CMake >= 3.24, Qt 6.10+; see README)" >&2
        exit 127
    fi
    if [ "$force_configure" -eq 1 ] || [ ! -f "build/$preset/CMakeCache.txt" ]; then
        cmake --preset "$preset"
    fi
    cmake --build --preset "$preset"
fi

if [ ! -x "$app" ]; then
    echo "run.sh: no binary at $app (try ./run.sh --build)" >&2
    exit 1
fi

exec "$app" "$@"
