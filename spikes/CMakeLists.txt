# SPDX-License-Identifier: MIT
# Spike executables. These are throwaway verification programs (not app code):
# they exist to answer a ticket question empirically and are kept in-tree so the
# answer stays reproducible. Never link app code to a spike target.

option(KOALAAMP_BUILD_SPIKES "Build the M0 spike executables" ON)

if(KOALAAMP_BUILD_SPIKES)
    add_subdirectory(fe1)
endif()
