# SPDX-License-Identifier: MIT
# AU-2 spike — gapless boundary probe (docs/tickets/AU-2-gapless-replaygain.md).
# Answers empirically: what does each candidate handover cost on the DSP path?
add_executable(spike_au2 main.cpp)
koalaamp_enable_warnings(spike_au2)
target_link_libraries(spike_au2 PRIVATE koalaamp_vlc)
