cmake_minimum_required(VERSION 3.20)

# configure.py writes build.ninja to the source root, so configuring here
# would clobber the matching build
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not supported; use a preset "
        "(cmake --preset native) or a separate binary directory")
endif()

project(melee LANGUAGES C)

include(GNUInstallDirs)

option(MELEE_DWARF "Emit DWARF describing the original type layouts" OFF)
option(MELEE_DAT_SAMPLES "Sample the .dat archives' types for objdiff (needs MELEE_DWARF)" OFF)
option(MELEE_PORT "Build the native port: the game, Aurora and the visionOS app" OFF)

if(MELEE_PORT)
    # Before MeleeConfig: it builds the game against the Aurora this fetches
    include(cmake/Port.cmake)
else()
    include(cmake/MeleeConfig.cmake)
endif()

if(MELEE_DWARF)
    include(cmake/Dwarf.cmake)
endif()

if(MELEE_DAT_SAMPLES)
    include(cmake/Dat.cmake)
endif()

if(NOT MELEE_PORT)
    install(TARGETS melee LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
