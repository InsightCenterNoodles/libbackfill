cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

set(CMAKE_C_COMPILER   clang  CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER clang++ CACHE STRING "" FORCE)


include(cmake/CPM.cmake)

CPMAddPackage(
    NAME SDL
    URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.18.tar.gz
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
        "SDL_SHARED OFF"
        "SDL_STATIC ON"
    EXCLUDE_FROM_ALL YES
)

set_property(TARGET SDL3-static PROPERTY POSITION_INDEPENDENT_CODE ON)


# Relax Filament's deprecated warnings-as-errors on macOS 15 Metal
set(_no_error_depr "-Wno-error=deprecated-declarations")


CPMAddPackage(
    NAME filament
    URL https://github.com/google/filament/archive/refs/tags/v1.66.2.tar.gz
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
        "FILAMENT_SKIP_SAMPLES ON"
        "CMAKE_COMPILE_WARNING_AS_ERROR OFF"
        "CMAKE_C_FLAGS ${_no_error_depr}"
        "CMAKE_CXX_FLAGS ${_no_error_depr}"
        "CMAKE_OBJCXX_FLAGS ${_no_error_depr}" # important for Metal (.mm) files
    EXCLUDE_FROM_ALL YES
)

CPMAddPackage(
    NAME spdlog
    URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
    OPTIONS
        "BUILD_SHARED_LIBS OFF"
        "SPDLOG_BUILD_PIC ON"
    EXCLUDE_FROM_ALL YES
)


if (UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Project ======================================================================

project(
    backfill_renderer
    VERSION 0.1
    LANGUAGES C CXX
)

# Library Target ===============================================================

set(BACKFILL_SOURCES
    src/generated.h src/generated.cpp
    src/geometry.h  src/geometry.cpp
    src/projection.h src/projection.cpp
    src/utility.h
    src/config.h
    src/localplatform.h src/localplatform.cpp
    src/localengine.h   src/localengine.cpp
    src/localrenderer.h src/localrenderer.cpp
    src/renderstate.h   src/renderstate.cpp
    src/session.h       src/session.cpp
    src/api.cpp
)

add_library(backfill SHARED ${BACKFILL_SOURCES})

set_target_properties(backfill PROPERTIES CXX_STANDARD 20)

# Disable warnings for use of the embedded directive
target_compile_options(backfill PRIVATE -Wno-c23-extensions)

# Force for all platforms. Hard requirement for Filament.
# Linux tries to build with libstdc++.
target_compile_options(backfill PRIVATE -stdlib=libc++)
target_link_options(backfill PRIVATE -stdlib=libc++)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")

if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
    target_compile_options(backfill PUBLIC -mavx)
endif ()

# Public headers live under include/backfill/*.h
target_include_directories(backfill
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Attach public headers to the target so IDEs show them
target_sources(backfill
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/backfill/api.h>
)

if(APPLE)
    # Make the install_name be @rpath/libbackfill_full.dylib so dependents can find it via their rpaths.
    set_target_properties(backfill PROPERTIES
        MACOSX_RPATH     ON
        INSTALL_NAME_DIR "@rpath"
    )
elseif(LINUX)
    # Executables depending on this can find it relative to themselves when installed.
    # (Consumers can still override/extend their own RPATHs.)
    set_target_properties(backfill PROPERTIES INSTALL_RPATH "\$ORIGIN")
endif()

# Link third-party deps to the library
target_link_libraries(backfill
    PRIVATE
        filament
        utils
        geometry
        backend
        image
        imageio
        ibl
        filament-iblprefilter
        SDL3-static
        spdlog::spdlog_header_only
)

# Platform bits the lib needs (propagated to consumers)
if(APPLE)
    target_link_libraries(backfill PUBLIC
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework CoreHaptics"
        "-framework GameController"
        "-framework ForceFeedback"
        "-framework Metal"
        "-framework MetalKit"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework QuartzCore"
        "-framework IOKit"
        "-framework Cocoa"
        "-framework Carbon"
        "-framework UniformTypeIdentifiers"
    )
endif()

if(WIN32)
    # Export all symbols automatically for a quick DLL (replace with explicit exports later).
    set_target_properties(backfill PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# For dev quality of life ======================================================

set(MATERIAL_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/materials/bakedColor.mat
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/materials/primaryLit.mat
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/materials/primaryInstanceLit.mat
)



add_custom_target(materials SOURCES ${MATERIAL_FILES})

source_group("Materials" FILES ${MATERIAL_FILES})

# Text exe =====================================================================
add_executable(backfill_demo
    examples/demo.cpp
)

set_target_properties(backfill_demo PROPERTIES CXX_STANDARD 20)

target_compile_options(backfill_demo PRIVATE -stdlib=libc++)
target_link_options(backfill_demo PRIVATE -stdlib=libc++)


if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
    target_compile_options(backfill_demo PUBLIC -mavx)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Enabling AddressSanitizer")
    target_compile_options(backfill_demo PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(backfill_demo   PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# Include path for your app sources if they need it
target_include_directories(backfill_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link the app against your library (which drags in Filament/SDL/etc).
target_link_libraries(backfill_demo PRIVATE backfill spdlog::spdlog_header_only)

# Install ======================================================================

include(GNUInstallDirs)


# Install public headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.hpp"
            PATTERN "*.hh"
          PATTERN ".*" EXCLUDE        # exclude all dotfiles (e.g. .DS_Store)
    )

# Install the shared library target itself
install(TARGETS backfill
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}   # .dll/.exe helpers (Windows)
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}   # .so/.dylib
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}   # import libs / static fallback
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Configure pkg-config file
set(prefix ${CMAKE_INSTALL_PREFIX})
set(exec_prefix ${CMAKE_INSTALL_PREFIX})
set(libdir ${CMAKE_INSTALL_FULL_LIBDIR})
set(includedir ${CMAKE_INSTALL_FULL_INCLUDEDIR})

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/backfill.pc.in
  ${CMAKE_CURRENT_BINARY_DIR}/backfill.pc
  @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/backfill.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

