#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# fetch-corpus.sh — download SE-1's 30-skin quirk corpus into the gitignored
# third_party/skins-corpus/ (docs/tickets/SE-1-wsz-loader.md).
#
# Sources: the Winamp Skin Museum's public CDN. Each skin is addressed by its
# MD5 (the museum's content hash, which is also the dedupe key), and the
# download is re-hashed on arrival — a mismatch is a hard error. The artwork
# remains each author's copyright: these files are **test fixtures only** and
# are never committed (AGENTS.md, LICENSES.md).
#
# Per-skin license/attribution notes (museum metadata: filename + readme author)
# are recorded in docs/tickets/SE-1-wsz-loader.md, not in this repo's sources.
#
# Usage:
#   scripts/fetch-corpus.sh [dest-dir]     # default third_party/skins-corpus
#   scripts/fetch-corpus.sh --list         # print the manifest, fetch nothing
#   scripts/fetch-corpus.sh --only <md5>   # fetch a single entry
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
catalog="$(mktemp)"
trap 'rm -f "${catalog}"' EXIT
cat >"${catalog}" <<'CATALOG'
5e4f10275dcb1fb211d4a8b4f1bda236|base-2.91.wsz|canonical Nullsoft base skin; 10 BI_RLE8 sheets
3296b2c6f6c064416425d3382fd2e2b8|My_First_Skin_for_RealOne.wsz|RLE8-heavy; repeated region sections; folder nesting; zero-size member
9dee1d5b8d326bdf2e35033f02e904f6|gundam_wing_zero_skin.wsz|RLE8-heavy; main.bmp 277x118 (oversized -> crop)
c151908ab31833320b8564d00926cd36|C-AMP2002.wsz|RLE8-heavy; 8-bit only
89086b613253aead4bdca9ff97f16bf4|Tostie_Skin.wsz|RLE8-heavy; mixed 8/24-bit sheets
c346918d19112d063f23b2569eb543f2|NapsterAmp.wsz|RLE8 + 4 region sections; folder nesting; zero-size member
546f302a8766bad93bb75893411e60d9|calvin_hobbes.wsz|RLE8 + no region.txt; folder nesting; zero-size member
0050f442e1438c491cb450f9fae047e4|Paper-Amp.wsz|RLE8-only sheets; folder nesting
a764732f6cf9be4aaa8c3f66279abbde|Future_DOS.wsz|1-bit main.bmp (B/W UI)
412572dfad47302cf1190a9d57530520|2a03.wsz|4-bit main.bmp + BI_RLE4 members
21ac871ab461e1ff869829a8e4f9ee83|WinXP_Amp_Platinum.wsz|BI_RLE4 members alongside 24-bit art
33ff5ec701bb02c577042976b6cd847c|Aqua.wsz|1/4/8-bit sheets; 4 region sections; zero-size member
3505c140b447fb42b942e06b707648c4|Fun_in_a_UFO.wsz|1/4/8-bit sheets; zero-size member; folder nesting
3b3b8b07fb7d268f6092d4321f0a9b9c|Necromech.wsz|8/24/32-bit mix + RLE8; region.txt
69c6aa22b63ac6772bd2db1e678e1963|Foot_Fetish.wsz|32-bit sheets; main.bmp 276x116; 200-point region; 31 cursors
7b39f961b0d80a96df69e447280e70a7|Ghost_Semi-Transparency_Demonstrator.wsz|region.txt-heavy: ~1925 vertices
be2de111c4710af306fea0813440f275|Microchip_2_Final_Tranceformation.wsz|region.txt-heavy: ~1243 vertices, 3 sections
971c1afc05b56f5160903d0748b73239|Deus_Ex_Amp_by_AJ.wsz|region.txt-heavy: ~525 vertices, 4 sections
07d31a296dc02a021fa6ddbcffeb5c5f|Donkey_Kong_Handheld.wsz|32-bit main; 4 region sections; gen/genex/video present
2191f8d61c84edb4eb37ecf21d411d55|Sheep_in_the_Big_City_1.wsz|4 region sections but no NumPoints/PointList (leniency)
0eaba96cdab9ebac4d72f852aef6c69b|AMPBOY.wsz|region [Playlist] section; missing eq_ex; folder nesting; zero-size member
1fc68f1376e8e1a7e15de659b3c89f36|fran_by_malionette.zip|missing eq_ex; titlebar.bmp 1x1 (hole); 186-point region; nested
211eb095dea514a244e0d57b824ac87e|Star_Trek_console.wsz|missing eq_ex and nums_ex.bmp
bba8100491e3785bb4e7726092693b4b|Simpsons.wsz|missing eqmain/balance/pledit/eq_ex
0e38ac23c5e02cba57b0825516be5279|ie.wsz|missing numbers.bmp (nums_ex fallback)
003dee624491506e1d736c6047ed1893|1.8.wsz|main.bmp 307x116 (oversized -> crop); 28 cursors
2c4f1e5e7d2a1d385bb186dd057df8d6|Integra_Mugen_Style.wsz|main.bmp 275x348 (oversized -> crop)
1c3c947dbcd5935714dd33ed5e0c072e|lightning1.wsz|main.bmp 275x112 (undersized -> hole); 4 region sections
29bc9754ba7946311183a1acfc3347a2|Hooplife_Dogslife.wsz|28 cursors; every canonical bitmap present
4269b10d8d27bd201f8608c59295680c|DOOM_II_AMP.wsz|4 zero-size members; folder nesting
CATALOG

dest="${root}/third_party/skins-corpus"
only=""
case "${1:-}" in
    --list|--manifest)
        awk -F'|' '{printf "%-32s  %-40s  %s\n", $1, $2, $3}' "${catalog}"
        exit 0
        ;;
    --only)
        only="${2:?--only needs an md5}"
        ;;
    "")
        ;;
    *)
        dest="$1"
        ;;
esac

base_url="https://r2.webampskins.org/skins"
manifest="${dest}/MANIFEST.tsv"

mkdir -p "${dest}"
tmp_manifest="$(mktemp)"
printf 'md5\tmuseum_file\tquirk\n' >"${tmp_manifest}"

fetched=0
skipped=0
failed=0

while IFS='|' read -r md5 museum_name quirk; do
    [[ -z "${md5}" ]] && continue
    if [[ -n "${only}" && "${only}" != "${md5}" ]]; then
        continue
    fi
    printf '%s\t%s\t%s\n' "${md5}" "${museum_name}" "${quirk}" >>"${tmp_manifest}"

    out="${dest}/${md5}.wsz"
    if [[ -s "${out}" ]] && [[ "$(md5sum "${out}" | cut -d' ' -f1)" == "${md5}" ]]; then
        skipped=$((skipped + 1))
        continue
    fi

    if ! curl -fsSL --retry 3 --retry-delay 1 -o "${out}.tmp" "${base_url}/${md5}.wsz"; then
        echo "fetch-corpus: download failed for ${md5} (${museum_name})" >&2
        rm -f "${out}.tmp"
        failed=$((failed + 1))
        continue
    fi
    actual="$(md5sum "${out}.tmp" | cut -d' ' -f1)"
    if [[ "${actual}" != "${md5}" ]]; then
        echo "fetch-corpus: MD5 mismatch for ${md5} (got ${actual})" >&2
        rm -f "${out}.tmp"
        failed=$((failed + 1))
        continue
    fi
    mv "${out}.tmp" "${out}"
    fetched=$((fetched + 1))
done <"${catalog}"

mv "${tmp_manifest}" "${manifest}"

echo "fetch-corpus: ${fetched} fetched, ${skipped} up-to-date, ${failed} failed -> ${dest}"
if [[ "${failed}" -ne 0 ]]; then
    exit 1
fi
