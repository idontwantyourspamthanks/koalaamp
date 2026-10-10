# SPDX-License-Identifier: MIT
# Shared warning configuration. `koalaamp_enable_warnings(<target>)` turns on a
# conservative cross-compiler set; keep it warning-clean from day one.

function(koalaamp_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
    endif()
endfunction()
