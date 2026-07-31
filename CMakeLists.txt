cmake_minimum_required(VERSION 3.20)

# tgfx 适配器需要 Objective-C++（Metal 后端），Apple 平台一并启用。
if (APPLE)
    project(MotionStudio LANGUAGES C CXX OBJC OBJCXX)
else()
    project(MotionStudio LANGUAGES C CXX)
endif()

if(CMAKE_GENERATOR MATCHES "Xcode")
  include("${CMAKE_SOURCE_DIR}/cmake/apple_xcode.cmake")
endif()

include("${CMAKE_SOURCE_DIR}/cmake/Utils.cmake")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(MOTIONSTUDIO_ENABLE_ASAN "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(MOTIONSTUDIO_ENABLE_ASAN)
  add_compile_options(-fno-omit-frame-pointer -fsanitize=address -fsanitize=undefined)
  add_link_options(-fsanitize=address -fsanitize=undefined)
endif()

add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/json EXCLUDE_FROM_ALL)

add_subdirectory(src)

# Boxed text layout (Core-independent; used by tgfx adapter / bridge).
add_subdirectory(adapter/textlayout)

enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)

if (APPLE)
    # Single tgfx pin: vendored under libpag. Consumed by adapter/tgfx prebuild,
    # adapter/pag_codec, bridge, and avf via these TGFX_* / LIBPAG_DIR vars.
    set(LIBPAG_DIR "${PROJECT_SOURCE_DIR}/third_party/libpag")
    set(TGFX_SOURCE_DIR "${LIBPAG_DIR}/third_party/tgfx")
    set(TGFX_INCLUDE_DIR "${TGFX_SOURCE_DIR}/include")
    set(TGFX_OUT_DIR "${CMAKE_BINARY_DIR}/tgfx_prebuilt")
    set(TGFX_CMAKE_ARGS "-DTGFX_USE_METAL=ON -DTGFX_USE_OPENGL=OFF -DTGFX_USE_PNG_DECODE=ON -DTGFX_USE_PNG_ENCODE=ON -DTGFX_USE_JPEG_DECODE=ON -DTGFX_USE_JPEG_ENCODE=ON -DTGFX_USE_WEBP_DECODE=ON -DTGFX_USE_WEBP_ENCODE=ON")

    add_subdirectory(adapter/tgfx)
    add_subdirectory(adapter/avf)
    # libpag base+codec only (no rendering/GL); not via upstream libpag CMake.
    add_subdirectory(adapter/pag_codec)
    # Document → .pag (PRIVATE pag_codec); after pag_codec.
    add_subdirectory(src/export/pag)

    # C ABI bridge for platform UI shells; must come after adapter/tgfx on Apple
    # (the canvas API links the tgfx adapter).
    add_subdirectory(bridge)
endif()


