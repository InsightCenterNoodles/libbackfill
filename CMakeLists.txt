cmake_minimum_required(VERSION 3.19 FATAL_ERROR)

set(THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party/)

list(PREPEND CMAKE_PREFIX_PATH ${THIRD_PARTY_DIR})

if (UNIX AND NOT APPLE)
    set(LINUX TRUE)
endif()

# Project ======================================================================

project(
    fill_cpp_test
    VERSION 0.1
    LANGUAGES C CXX
)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "You must compile this with clang")
endif()

# Create BIN ===============================================================

add_executable(ftest
    src/main.cpp
    src/generated.h src/generated.cpp
    src/geometry.h src/geometry.cpp
    src/projection.h src/projection.cpp
    src/utility.h
    src/config.h
)

# For dev experience ===========================================================

set(MATERIAL_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/materials/bakedColor.mat
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/materials/primaryLit.mat
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/materials/primaryInstanceLit.mat
)


add_custom_target(materials SOURCES ${MATERIAL_FILES})

source_group("Materials" FILES ${MATERIAL_FILES})

set(BUILD_SHARED_LIBS OFF)

set_target_properties(ftest PROPERTIES CXX_STANDARD 20)
target_compile_options(ftest PRIVATE -Wno-c23-extensions)
target_compile_options(ftest PRIVATE -stdlib=libc++)
target_link_options(ftest PRIVATE -stdlib=libc++)

if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
    set (X86 TRUE)
    target_compile_options(ftest PUBLIC -mavx)
else ()
    set (X86 FALSE)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Enabling AddressSanitizer")
    target_compile_options(ftest PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(ftest PUBLIC  -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# Source Files =================================================================

set(CMAKE_INCLUDE_CURRENT_DIR ON)

target_include_directories(ftest PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
target_include_directories(ftest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/include/)

# Link Deps ====================================================================

# Link Util ====================================================================

function(find_and_set_filament_libs target lib_names)
    set(MAIN_LIB_DIR "${THIRD_PARTY_DIR}/lib")
    set(FILAMENT_DIR_x86 "${THIRD_PARTY_DIR}/lib/x86_64")
    set(FILAMENT_DIR_arm "${THIRD_PARTY_DIR}/lib/arm64")

    set(FOUND_LIBS "")

    foreach(lib_name IN LISTS lib_names)

        find_library(found_lib
            NAMES ${lib_name} ${lib_name}.a
            PATHS ${FILAMENT_DIR_x86} ${FILAMENT_DIR_arm} ${MAIN_LIB_DIR}
            REQUIRED
            NO_CACHE
        )

        list(APPEND FOUND_LIBS ${found_lib})
        unset(found_lib)
    endforeach()

    target_link_libraries(${target} PRIVATE
        ${FOUND_LIBS}
    )

    message("${FOUND_LIBS}")
endfunction()

set(REQUIRED_LIBS
    abseil
    basis_transcoder
    camutils
    civetweb
    dracodec
    filamat
    filament
    filaflat
    filabridge
    filament-iblprefilter
    filameshio
    backend
    bluegl
    bluevk
    geometry
    gltfio
    gltfio_core
    ibl
    ibl-lite
    image
    ktxreader
    matdbg
    meshoptimizer
    mikktspace
    perfetto
    shaders
    smol-v
    stb
    uberarchive
    uberzlib
    utils
    viewer
    vkshaders
    SDL3
    zstd
)

find_and_set_filament_libs(ftest "${REQUIRED_LIBS}")

if (NOT LINUX)
    #target_link_libraries(ftest PRIVATE "-framework Metal -framework QuartzCore")
    target_link_libraries(ftest PRIVATE
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

