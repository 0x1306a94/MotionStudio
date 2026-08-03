# pag_codec — MotionStudio-owned subset of libpag (base + codec only).
# No libpag rendering / platform GPU. Links the same prebuilt tgfx used by
# adapter/tgfx.
#
# Requires (from root CMakeLists + adapter/tgfx):
#   LIBPAG_DIR / TGFX_INCLUDE_DIR
#   motionstudio_add_tgfx_prebuild / motionstudio_link_prebuilt_tgfx

if(NOT LIBPAG_DIR OR NOT TGFX_INCLUDE_DIR)
  message(FATAL_ERROR "adapter/pag_codec requires LIBPAG_DIR and TGFX_INCLUDE_DIR from root CMakeLists")
endif()

add_files_by_extension(PAG_CODEC_SOURCES ".h;.mm;.cpp"
    ${LIBPAG_DIR}/src/base
    ${LIBPAG_DIR}/src/codec
)

# Default Platform virtuals (typeinfo / naluType / …). Current() + VideoDecoderFactory
# stubs live in src/PlatformStub.cpp so we do not pull NativePlatform.
list(APPEND PAG_CODEC_SOURCES
     "${LIBPAG_DIR}/src/platform/Platform.cpp"
     "${CMAKE_CURRENT_SOURCE_DIR}/src/PlatformStub.cpp")

add_files_by_extension(PAG_CODEC_PUBLIC_HEADERS ".h"
    ${LIBPAG_DIR}/include/pag
)

add_library(pag_codec STATIC ${PAG_CODEC_PUBLIC_HEADERS} ${PAG_CODEC_SOURCES})
set_target_properties(pag_codec PROPERTIES OUTPUT_NAME pag_codec)
add_source_group(PAG_CODEC_SOURCES ${LIBPAG_DIR}/src "Sources")
add_source_group(PAG_CODEC_PUBLIC_HEADERS ${LIBPAG_DIR}/include "Public Headers")
source_group("Stub" FILES
             "${LIBPAG_DIR}/src/platform/Platform.cpp"
             "${CMAKE_CURRENT_SOURCE_DIR}/src/PlatformStub.cpp")

target_include_directories(pag_codec
                           PUBLIC
                           "${LIBPAG_DIR}/include"
                           PRIVATE
                           "${LIBPAG_DIR}/src"
                           "${TGFX_INCLUDE_DIR}")

target_compile_features(pag_codec PUBLIC cxx_std_17)
# Upstream libpag enables -Werror; keep MS build resilient to third_party noise.
target_compile_options(pag_codec PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(pag_codec
                      PUBLIC
                      "-framework Foundation"
                      "-framework Metal"
                      "-framework MetalKit"
                      "-framework QuartzCore"
                      "-framework CoreGraphics"
                      "-framework CoreText"
                      "-framework CoreVideo"
                      "-framework CoreMedia"
                      "-framework ImageIO"
                      iconv)
if(NOT CMAKE_GENERATOR MATCHES "Xcode")
  target_link_libraries(pag_codec
                        PUBLIC
                        "-framework ApplicationServices"
                        "-framework Cocoa")
else()
  target_link_libraries(pag_codec PUBLIC "-framework UIKit")
endif()

motionstudio_add_tgfx_prebuild(pag_codec)
motionstudio_link_prebuilt_tgfx(pag_codec)

include(GoogleTest)
add_files_by_extension(PAG_CODEC_TEST_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
add_executable(pag_codec_test ${PAG_CODEC_TEST_SOURCES})
add_source_group(PAG_CODEC_TEST_SOURCES ${CMAKE_CURRENT_LIST_DIR}/tests "Tests")
disable_xcode_target_signing(pag_codec_test)
target_link_libraries(pag_codec_test PRIVATE pag_codec GTest::gtest_main)
# Static pag_codec carries PUBLIC -ltgfx; the executable still needs Xcode
# LIBRARY_SEARCH_PATHS (and PRE_BUILD) — same pattern as tgfx_adapter_test.
motionstudio_add_tgfx_prebuild(pag_codec_test)
motionstudio_link_prebuilt_tgfx(pag_codec_test)
gtest_discover_tests(pag_codec_test)

# Browse-only: surface vendored pag sources in the Xcode navigator without compiling.
if(CMAKE_GENERATOR MATCHES "Xcode")
  add_files_by_extension(PAG_BROWSE_SOURCES ".h;.hpp;.c;.cc;.cpp;.m;.mm"
      ${PAG_SOURCE_DIR})
  add_files_by_extension(PAG_BROWSE_HEADERS ".h;.hpp"
      ${PAG_INCLUDE_DIR})
  add_custom_target(pag_sources SOURCES ${PAG_BROWSE_SOURCES} ${PAG_BROWSE_HEADERS})
  add_source_group(PAG_BROWSE_SOURCES ${PAG_SOURCE_DIR} "Sources")
  add_source_group(PAG_BROWSE_HEADERS ${PAG_INCLUDE_DIR} "Public Headers")
endif()
