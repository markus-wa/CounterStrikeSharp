# Monocypher 4.0.2 (https://monocypher.org, BSD-2-Clause or CC0, see LICENCE.md), vendored unmodified from the release
# tarball monocypher-4.0.2.tar.gz (sha256 38d07179738c0c90677dba3ceb7a7b8496bcfea758ba1a53e803fed30ae0879c): src/ and
# src/optional/ flattened into this directory. Verifies Gamedata Feed signatures with its Ed25519 (SHA-512) variant
# (src/core/gamedata_feed.cpp, ADR-0006).
#
# A static library with hidden symbols, so it never clashes with another copy in the process.
if(NOT TARGET monocypher)
    add_library(monocypher STATIC ${CMAKE_CURRENT_LIST_DIR}/monocypher.c ${CMAKE_CURRENT_LIST_DIR}/monocypher-ed25519.c)
    target_include_directories(monocypher PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    set_target_properties(monocypher PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_VISIBILITY_PRESET hidden
    )
    if(MSVC)
        target_compile_options(monocypher PRIVATE /w)
    else()
        target_compile_options(monocypher PRIVATE -w)
    endif()
endif()
