#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# run-spike.sh — run the FE-14 projectM spike headlessly under Xvfb with
# software GL (llvmpipe). Sets the Qt platform/GL-integration env the spike
# needs on X11/Xvfb. All arguments are forwarded to spike_fe14.
#
# Usage: spikes/fe14_projectm/run-spike.sh --mode masked --duration 30
# Env:   FE14_BIN    override the spike binary path
#        FE14_SCREEN Xvfb screen geometry (default 1920x1080x24)
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
bin="${FE14_BIN:-${root}/build/linux-gcc/spikes/fe14_projectm/spike_fe14}"
screen="${FE14_SCREEN:-1920x1080x24}"

if [[ ! -x "${bin}" ]]; then
    echo "run-spike: spike binary not found: ${bin}" >&2
    echo "           build it first: cmake --build --preset linux-gcc (needs scripts/vendor-deps.sh)" >&2
    exit 1
fi

exec xvfb-run -a -s "-screen 0 ${screen}" \
    env QT_QPA_PLATFORM=xcb QT_XCB_GL_INTEGRATION=xcb_glx QT_OPENGL=desktop \
    "${bin}" "$@"
