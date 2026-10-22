#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# fetch-textures.sh — download the pinned projectM MilkDrop texture pack into the
# gitignored third_party/ tree for local visualizer runs and soak testing.
#
# The pack has no licence file, so it is not committed or redistributed by this
# repository; packagers must establish redistribution terms independently.
#
# Usage: scripts/fetch-textures.sh [dest-dir]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dest="${1:-${root}/third_party/textures}"
commit="6368812f27bc747b517218fbf89d21d59afce4d9"
url="https://codeload.github.com/projectM-visualizer/presets-milkdrop-texture-pack/tar.gz/${commit}"

if [ -d "$dest" ] && find "$dest" -type f | grep -q .; then
    echo "fetch-textures: $dest already populated; nothing to do"
    exit 0
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
echo "fetch-textures: downloading ${commit:0:8} ..."
curl -fsSL "$url" -o "$tmp/pack.tar.gz"
tar -xzf "$tmp/pack.tar.gz" -C "$tmp"
mkdir -p "$dest"
src="$(find "$tmp" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
if [ -z "$src" ]; then
    echo "fetch-textures: pack layout not recognised" >&2
    exit 1
fi
cp -a "$src"/. "$dest"/
echo "fetch-textures: $(find "$dest" -type f | wc -l) files in $dest"
