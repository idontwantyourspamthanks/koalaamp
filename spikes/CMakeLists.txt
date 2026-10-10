# SPDX-License-Identifier: MIT
# Throwaway spike executables — each answers a ticket question empirically and
# stays in-tree so the answer stays reproducible. Never link app code to a
# spike target. Subdirs skip themselves cleanly when their native dependency
# is not vendored (see scripts/vendor-deps.sh).

option(KOALAAMP_BUILD_SPIKES "Build the M0 spike executables" ON)

if(KOALAAMP_BUILD_SPIKES)
    add_subdirectory(fe1)
endif()
add_subdirectory(fe14_projectm)
