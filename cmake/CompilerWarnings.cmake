# Centralized, strict compiler diagnostics for every first-party target.
function(secretsimage_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)
        if(SECRETSIMAGE_WERROR)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wconversion -Wsign-conversion -Wshadow
            -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
            -Wunused -Woverloaded-virtual -Wnull-dereference
            -Wdouble-promotion -Wformat=2
        )
        if(SECRETSIMAGE_WERROR)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    # Hardening and determinism friendly defaults for release binaries.
    if(NOT MSVC AND CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(${target} PRIVATE -O3 -fno-plt)
    endif()
endfunction()
