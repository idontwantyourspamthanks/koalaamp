#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# FE-1 X11 matrix driver.
#
# Runs spike_fe1 on a given $DISPLAY, then captures the three pieces of evidence
# the ticket asks for:
#   1. the X server's view of the shaped window   -> xwininfo -shape (SHAPE ext)
#   2. the rendered pixels                        -> x11grab of the window + probes
#   3. OS-level input clipping                    -> real xdotool clicks at a
#      point *outside* the polygon (must reach no-one) and one inside (must log)
#
# Window geometry is taken from the X server (xwininfo), not from the app, and
# physical coords are derived via the X-window size / logical-size ratio so the
# test also works under a scaling compositor (XWayland + KWin fractional scale).
#
# Usage: x11_matrix.sh <DISPLAY> <label> <outdir>
set -euo pipefail

DISPLAY_ARG="${1:?usage: x11_matrix.sh <DISPLAY> <label> <outdir>}"
LABEL="${2:?}"
OUT="${3:?}"
DURATION="${DURATION:-15}"
POLYGON="${POLYGON:-star}"

HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="${SPIKE_BIN:-$HERE/../../build/linux-gcc/spikes/fe1/spike_fe1}"
mkdir -p "$OUT"
LOG="$OUT/$LABEL.log"

export DISPLAY="$DISPLAY_ARG"
export QT_QPA_PLATFORM=xcb

"$BIN" --polygon "$POLYGON" --duration "$DURATION" --move 100,100 >"$LOG" 2>&1 &
PID=$!

# Wait for the window to identify itself.
for _ in $(seq 1 60); do grep -q '^SPIKE|points' "$LOG" && break; sleep 0.1; done
WINID="$(sed -n 's/.*winid=0x\([0-9a-f]*\).*/\1/p' "$LOG" | head -1)"
LX="$(sed -n 's/.*inside_local=\([0-9-]*\),\([0-9-]*\) .*/\1 \2/p' "$LOG" | head -1)"
OX="$(sed -n 's/.*outside_local=\([0-9-]*\),\([0-9-]*\) .*/\1 \2/p' "$LOG" | head -1)"
read -r ILX ILY <<<"$LX"
read -r OLX OLY <<<"$OX"

# Authoritative geometry from the X server; wait until it is viewable.
for _ in $(seq 1 60); do
    xwininfo -id "0x$WINID" >"$OUT/$LABEL.win.txt" 2>/dev/null || true
    grep -q 'Map State: IsViewable' "$OUT/$LABEL.win.txt" && break
    sleep 0.1
done
GX="$(awk '/Absolute upper-left X/{print $NF}' "$OUT/$LABEL.win.txt")"
GY="$(awk '/Absolute upper-left Y/{print $NF}' "$OUT/$LABEL.win.txt")"
GW="$(awk '/Width:/{print $NF}' "$OUT/$LABEL.win.txt")"
GH="$(awk '/Height:/{print $NF}' "$OUT/$LABEL.win.txt")"

echo "== $LABEL (DISPLAY=$DISPLAY_ARG) =="
echo "window=0x$WINID geom=${GW}x${GH}+${GX}+${GY} logical=275x116 inside_local=$ILX,$ILY outside_local=$OLX,$OLY"

# Physical coords for xdotool / pixel probes (X11 window is in device pixels).
phys() { awk -v g="$GX" -v s="$(awk -v w="$GW" 'BEGIN{print w/275.0}')" -v l="$1" 'BEGIN{printf "%d", g + l*s}'; }
PS="$(awk -v w="$GW" 'BEGIN{print w/275.0}')"
IXP="$(phys "$ILX")"; IYP="$(phys "$ILY")"
OXP="$(phys "$OLX")"; OYP="$(phys "$OLY")"
PILX="$(awk -v s="$PS" -v l="$ILX" 'BEGIN{printf "%d", l*s}')"
PILY="$(awk -v s="$PS" -v l="$ILY" 'BEGIN{printf "%d", l*s}')"
POLX="$(awk -v s="$PS" -v l="$OLX" 'BEGIN{printf "%d", l*s}')"
POLY="$(awk -v s="$PS" -v l="$OLY" 'BEGIN{printf "%d", l*s}')"

# 1) Shape: the X server's bounding shape (non-rectangular entries prove XShape
#    clipping is live, independent of any compositor).
xwininfo -shape -id "0x$WINID" >"$OUT/$LABEL.xwininfo.txt" 2>&1 || true
sed -n '/Window shape extents/p;/Corners/,+1p' "$OUT/$LABEL.xwininfo.txt" | head -4 || true

# 2) Pixels: grab exactly the window rect, then probe inside/outside points.
ffmpeg -loglevel error -y -f x11grab -draw_mouse 0 -video_size "${GW}x${GH}" \
       -i "${DISPLAY_ARG}.0+${GX},${GY}" -frames:v 1 "$OUT/$LABEL.screen.png"
PROBE_IN="$(convert "$OUT/$LABEL.screen.png" -format "%[pixel:p{$PILX,$PILY}]" info:)"
PROBE_OUT="$(convert "$OUT/$LABEL.screen.png" -format "%[pixel:p{$POLX,$POLY}]" info:)"
echo "pixel inside=$PROBE_IN  pixel outside=$PROBE_OUT"

# 3) Input clipping: click the outside point first — with XShape input clipping
#    the window never sees it (no SPIKE|press line); then click the inside point
#    to prove the harness does deliver presses.
xdotool mousemove --sync "$OXP" "$OYP" click 1
sleep 0.4
xdotool mousemove --sync "$IXP" "$IYP" click 1
sleep 0.4
wait "$PID"

echo "-- presses logged --"
grep -n 'SPIKE|press' "$LOG" || echo "(none)"
echo "-- fps --"
grep -n 'SPIKE|fps' "$LOG" || true
