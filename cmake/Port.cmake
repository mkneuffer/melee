# The native port: the game built for a 64-bit host against Aurora, its
# archives read through the native archive interface, and, on visionOS, the
# app around it (platform/visionos).
#
# Needs, from the player's own copy of the game:
# - build/GALE01/include: what the matching build extracts from main.dol
#   (the fonts); `python configure.py && ninja` makes it
# - build/GALE01/dat/types.bin: the types the dat preset exports, which the
#   archive schema is generated from; `cmake --preset dat && cmake --build
#   --preset dat` makes it
# Neither is in the repository, nor in what this builds but the app.
include_guard(GLOBAL)

include(FetchContent)

get_filename_component(_melee_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

set(MELEE_VERSION GALE01 CACHE STRING "Game version")
set(MELEE_EXTRACTED_INCLUDE "${_melee_root}/build/${MELEE_VERSION}/include"
    CACHE PATH "What the matching build extracted from main.dol")
if(NOT EXISTS "${MELEE_EXTRACTED_INCLUDE}/sysdolphin/baselib/sislib_font.inc")
    message(FATAL_ERROR
        "No fonts in ${MELEE_EXTRACTED_INCLUDE}: put the game's files in "
        "orig/${MELEE_VERSION} and run `python configure.py && ninja` first")
endif()

# Aurora, at the commit the dev shell pins (flake.nix), with the port's
# changes: a host-owned Metal layer and per-eye present targets
set(_aurora_patches
    "${_melee_root}/port/aurora/0001-external-layer-and-present-targets.patch")
FetchContent_Declare(aurora
    GIT_REPOSITORY https://github.com/r-burns/aurora.git
    GIT_TAG e6a6f02ace4146e8a2f648d5c274dbb7dd89665c
    GIT_SHALLOW FALSE
    PATCH_COMMAND git apply --whitespace=nowarn ${_aurora_patches}
    UPDATE_DISCONNECTED TRUE
)
set(AURORA_ENABLE_RMLUI OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(aurora)
set(AURORA_SRC "${aurora_SOURCE_DIR}" CACHE PATH "Aurora's source" FORCE)

include("${CMAKE_CURRENT_LIST_DIR}/MeleeConfig.cmake")

# The game, as the port builds it
file(GLOB PORT_SOURCES CONFIGURE_DEPENDS "${_melee_root}/src/port/*.c")
target_sources(melee PRIVATE ${PORT_SOURCES})
target_include_directories(melee_game_headers BEFORE INTERFACE
    "${_melee_root}/src/port/include"
)
target_include_directories(melee PRIVATE "${MELEE_EXTRACTED_INCLUDE}")
target_compile_options(melee PRIVATE
    "SHELL:-include ${_melee_root}/src/port/port_compat.h"
    # The decompiled code is as the original compiler took it
    -w
)
# C23, where bool is a keyword and bool=int stays an int (as the console's
# compiler had it), instead of <stdbool.h>'s 1-byte _Bool
set_target_properties(melee PROPERTIES C_STANDARD 23 C_EXTENSIONS ON)
target_link_libraries(melee PUBLIC aurora::core aurora::gx aurora::gd aurora::dvd
    aurora::card aurora::pad aurora::si aurora::vi aurora::os aurora::mtx)

# The archives' native types
set(MELEE_DAT_FILES "${_melee_root}/orig/${MELEE_VERSION}/files"
    CACHE PATH "The game's files")
add_subdirectory("${_melee_root}/tools/dat-cli/native" dat_native)
target_link_libraries(melee PUBLIC dat_melee)

if(CMAKE_SYSTEM_NAME STREQUAL "visionOS")
    add_subdirectory("${_melee_root}/platform/visionos" visionos)
endif()
