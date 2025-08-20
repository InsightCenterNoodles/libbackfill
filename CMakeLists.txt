cmake_minimum_required(VERSION 3.24 FATAL_ERROR)

include(cmake/CPM.cmake)

CPMAddPackage(
  NAME SDL
  URL https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.2.18.tar.gz
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
    "SDL_SHARED OFF"
    "SDL_STATIC ON"
)

# Relax Filament's deprecated warnings-as-errors on macOS 15 Metal
set(_no_error_depr "-Wno-error=deprecated-declarations")


CPMAddPackage(
  NAME filament
  URL https://github.com/google/filament/archive/refs/tags/v1.62.1.tar.gz
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
    "FILAMENT_SKIP_SAMPLES ON"
    "CMAKE_COMPILE_WARNING_AS_ERROR OFF"
    "CMAKE_C_FLAGS ${_no_error_depr}"
    "CMAKE_CXX_FLAGS ${_no_error_depr}"
    "CMAKE_OBJCXX_FLAGS ${_no_error_depr}" # important for Metal (.mm) files
)

CPMAddPackage(
  NAME spdlog
  URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
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

add_library(backfill STATIC ${BACKFILL_SOURCES})

set_target_properties(backfill PROPERTIES CXX_STANDARD 20)

# Disable warnings for use of the embedded directive
target_compile_options(backfill PRIVATE -Wno-c23-extensions)

# Force for all platforms. Linux tries to build with libstdc++.
target_compile_options(backfill PRIVATE -stdlib=libc++)
target_link_options(backfill PRIVATE -stdlib=libc++)

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

# Link third-party deps to the library
target_link_libraries(backfill
    PRIVATE
        filament
        utils
        geometry
        backend
        SDL3-static
        spdlog::spdlog
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

# ------------------------------
# Build the "fat" static archive by merging all the .a/.lib
# ------------------------------

set(_MERGE_INPUT_TARGETS
    backfill
    filament
    utils
    geometry
    backend
    spdlog
    SDL3-static
    vkshaders
    bluevk
)
set(_BACKFILL_STATIC_INPUTS)
foreach(tgt IN LISTS _MERGE_INPUT_TARGETS)
  if(TARGET ${tgt})
    list(APPEND _BACKFILL_STATIC_INPUTS $<TARGET_FILE:${tgt}>)
  endif()
endforeach()

if(NOT _BACKFILL_STATIC_INPUTS)
  message(FATAL_ERROR "No static inputs found to build fat archive.")
endif()

set(_FAT_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libbackfill_full${CMAKE_STATIC_LIBRARY_SUFFIX})

if(APPLE)
  # libtool is the reliable way to merge archives on macOS
  add_custom_command(
    OUTPUT ${_FAT_OUTPUT}
    COMMAND libtool -static -o ${_FAT_OUTPUT} ${_BACKFILL_STATIC_INPUTS}
    DEPENDS ${_BACKFILL_STATIC_INPUTS}
    VERBATIM
  )
elseif(WIN32)
  # MSVC librarian merges .lib files
  add_custom_command(
    OUTPUT ${_FAT_OUTPUT}
    COMMAND lib /OUT:${_FAT_OUTPUT} ${_BACKFILL_STATIC_INPUTS}
    DEPENDS ${_BACKFILL_STATIC_INPUTS}
    VERBATIM
  )
else()
  # Generic Unix (Linux, etc.): use ar MRI script, then ranlib
  set(_MRI ${CMAKE_CURRENT_BINARY_DIR}/merge_backfill.mri)
  add_custom_command(
    OUTPUT ${_FAT_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_FAT_OUTPUT}
    COMMAND ${CMAKE_COMMAND} -E echo "CREATE ${_FAT_OUTPUT}" > ${_MRI}
    # Add each input archive
    COMMAND ${CMAKE_COMMAND} -DINPUTS="$<JOIN:${_BACKFILL_STATIC_INPUTS},;>" -DMRI=${_MRI} -P ${CMAKE_CURRENT_LIST_DIR}/cmake/write_addlib_list.cmake
    COMMAND ${CMAKE_COMMAND} -E echo "SAVE" >> ${_MRI}
    COMMAND ${CMAKE_COMMAND} -E echo "END"  >> ${_MRI}
    COMMAND ${CMAKE_AR} -M < ${_MRI}
    COMMAND ${CMAKE_RANLIB} ${_FAT_OUTPUT}
    DEPENDS ${_BACKFILL_STATIC_INPUTS}
    VERBATIM
  )
  # The small helper script that writes ADDLIB lines (see below)
  file(WRITE ${CMAKE_CURRENT_LIST_DIR}/cmake/write_addlib_list.cmake
"string(REPLACE \";\" \"\\n\" _lines \"${INPUTS}\")\n
foreach(L IN LISTS _lines)\n
  if(NOT L STREQUAL \"\")\n
    file(APPEND ${MRI} \"ADDLIB ${L}\\n\")\n
  endif()\n
endforeach()\n")
endif()

add_custom_target(backfill_full ALL DEPENDS ${_FAT_OUTPUT})

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
target_link_libraries(backfill_demo PRIVATE backfill spdlog::spdlog)

# Install ======================================================================

include(GNUInstallDirs)

# Install the merged static archive
install(FILES ${_FAT_OUTPUT}
        DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Install public headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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

