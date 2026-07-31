# tgfx-based render adapter (Metal backend), Apple platforms only.
# tgfx is prebuilt per Destination via build_tgfx (not add_subdirectory).
# TGFX_SOURCE_DIR / TGFX_INCLUDE_DIR are set by the root CMakeLists.txt.

if(NOT TGFX_SOURCE_DIR OR NOT TGFX_INCLUDE_DIR OR NOT TGFX_OUT_DIR)
  message(FATAL_ERROR
          "adapter/tgfx requires TGFX_SOURCE_DIR, TGFX_INCLUDE_DIR, and TGFX_OUT_DIR from root CMakeLists")
endif()

if(NOT TGFX_CMAKE_ARGS)
  message(FATAL_ERROR "adapter/tgfx requires TGFX_CMAKE_ARGS from root CMakeLists")
endif()

# Defaults so Ninja/CI configure works; Xcode gen scripts can still override explicitly.
if(NOT MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET)
  set(MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "macOS deployment target for tgfx prebuild")
endif()
if(NOT MOTIONSTUDIO_IPHONEOS_DEPLOYMENT_TARGET)
  set(MOTIONSTUDIO_IPHONEOS_DEPLOYMENT_TARGET "18.0" CACHE STRING "iOS deployment target for tgfx prebuild")
endif()

# tgfx output directory config only — do not force CMAKE_BUILD_TYPE (Release + NDEBUG
# breaks assert / EXPECT_DEATH tests when CI leaves build type unset).
if(CMAKE_BUILD_TYPE)
  set(TGFX_SINGLE_CONFIG "${CMAKE_BUILD_TYPE}")
else()
  set(TGFX_SINGLE_CONFIG "Release")
endif()

if(CMAKE_GENERATOR MATCHES "Xcode")
  set(TGFX_PREBUILT_MAC_DIR "${TGFX_OUT_DIR}/$(CONFIGURATION)/mac/arm64")
  set(TGFX_PREBUILT_CATALYST_DIR "${TGFX_OUT_DIR}/$(CONFIGURATION)/catalyst/arm64")
  set(TGFX_PREBUILT_IOS_DIR "${TGFX_OUT_DIR}/$(CONFIGURATION)/ios/arm64")
  set(TGFX_PREBUILT_IOSSIM_DIR "${TGFX_OUT_DIR}/$(CONFIGURATION)/ios/arm64-simulator")

  # PARENT_SCOPE only writes to the parent; re-export the local values so sibling
  # directories (e.g. bridge) can read them without losing them in this scope.
  set(TGFX_PREBUILT_MAC_DIR "${TGFX_PREBUILT_MAC_DIR}" PARENT_SCOPE)
  set(TGFX_PREBUILT_CATALYST_DIR "${TGFX_PREBUILT_CATALYST_DIR}" PARENT_SCOPE)
  set(TGFX_PREBUILT_IOS_DIR "${TGFX_PREBUILT_IOS_DIR}" PARENT_SCOPE)
  set(TGFX_PREBUILT_IOSSIM_DIR "${TGFX_PREBUILT_IOSSIM_DIR}" PARENT_SCOPE)

  # Build the cmake -P argv inside the function so callers outside this directory
  # (e.g. adapter/pag_codec) still get a complete command. Directory-local lists
  # like _tgfx_prebuild_cmd are invisible when the function runs from elsewhere.
  function(motionstudio_add_tgfx_prebuild target_name)
    add_custom_command(TARGET ${target_name} PRE_BUILD
        COMMAND "${CMAKE_COMMAND}"
                "-DTGFX_SOURCE_DIR=${TGFX_SOURCE_DIR}"
                "-DTGFX_OUT_DIR=${TGFX_OUT_DIR}"
                "-DTGFX_CMAKE_ARGS=${TGFX_CMAKE_ARGS}"
                "-DTGFX_MACOSX_DEPLOYMENT_TARGET=${MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET}"
                "-DTGFX_IPHONEOS_DEPLOYMENT_TARGET=${MOTIONSTUDIO_IPHONEOS_DEPLOYMENT_TARGET}"
                "-DCMAKE_BUILD_TYPE=$<CONFIG>"
                -P "${PROJECT_SOURCE_DIR}/cmake/BuildTgfx.cmake"
        COMMENT "Prebuild tgfx for current PLATFORM_NAME / $<CONFIG>"
        VERBATIM)
  endfunction()

  function(motionstudio_link_prebuilt_tgfx target_name)
    set_target_properties(${target_name} PROPERTIES
        # gen_mac has no native macosx destination; sdk=macosx* is Mac Catalyst.
        XCODE_ATTRIBUTE_LIBRARY_SEARCH_PATHS[sdk=macosx*] "${TGFX_PREBUILT_CATALYST_DIR}"
        XCODE_ATTRIBUTE_LIBRARY_SEARCH_PATHS[sdk=iphoneos*] "${TGFX_PREBUILT_IOS_DIR}"
        XCODE_ATTRIBUTE_LIBRARY_SEARCH_PATHS[sdk=iphonesimulator*] "${TGFX_PREBUILT_IOSSIM_DIR}")
    target_link_libraries(${target_name} PUBLIC "-ltgfx")
  endfunction()
else()
  # Ninja/Make: declare OUTPUT so the build graph knows how to produce tgfx.a.
  # USES_TERMINAL → Ninja console pool so prebuild logs stream live.
  find_program(NODE_EXECUTABLE NAMES node REQUIRED)

  set(TGFX_PREBUILT_MAC_DIR "${TGFX_OUT_DIR}/${TGFX_SINGLE_CONFIG}/mac/arm64")
  set(TGFX_PREBUILT_MAC_LIB "${TGFX_PREBUILT_MAC_DIR}/tgfx.a")
  set(TGFX_PREBUILT_MAC_OUT "${TGFX_OUT_DIR}/${TGFX_SINGLE_CONFIG}/mac")
  set(TGFX_PREBUILT_MAC_DIR "${TGFX_PREBUILT_MAC_DIR}" PARENT_SCOPE)
  set(TGFX_PREBUILT_MAC_LIB "${TGFX_PREBUILT_MAC_LIB}" PARENT_SCOPE)

  separate_arguments(_tgfx_cmake_args_list NATIVE_COMMAND "${TGFX_CMAKE_ARGS}")

  # This rule only runs when tgfx.a is missing/out-of-date. Clear build_tgfx's
  # incremental stamp so a manual delete of tgfx.a still forces a real rebuild.
  add_custom_command(
      OUTPUT "${TGFX_PREBUILT_MAC_LIB}"
      COMMAND "${CMAKE_COMMAND}" -E echo "::group::Prebuild tgfx (${TGFX_SINGLE_CONFIG}/mac/arm64)"
      COMMAND "${CMAKE_COMMAND}" -E rm -f "${TGFX_PREBUILT_MAC_OUT}/.tgfx.arm64.md5"
      COMMAND
        "${NODE_EXECUTABLE}" "${TGFX_SOURCE_DIR}/build_tgfx" tgfx
        -s "${TGFX_SOURCE_DIR}"
        -p mac
        -a arm64
        -o "${TGFX_PREBUILT_MAC_OUT}"
        -i
        ${_tgfx_cmake_args_list}
        "-DCMAKE_BUILD_TYPE=${TGFX_SINGLE_CONFIG}"
        "-DCMAKE_OSX_DEPLOYMENT_TARGET=${MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET}"
        "-DDEPLOYMENT_TARGET=${MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET}"
      COMMAND "${CMAKE_COMMAND}" -E chdir "${TGFX_PREBUILT_MAC_DIR}"
              "${CMAKE_COMMAND}" -E create_symlink tgfx.a libtgfx.a
      COMMAND "${CMAKE_COMMAND}" -E echo "::endgroup::"
      WORKING_DIRECTORY "${TGFX_SOURCE_DIR}"
      COMMENT "Prebuild tgfx for mac / ${TGFX_SINGLE_CONFIG}"
      USES_TERMINAL
      VERBATIM)

  add_custom_target(tgfx_prebuilt_mac DEPENDS "${TGFX_PREBUILT_MAC_LIB}")

  function(motionstudio_add_tgfx_prebuild target_name)
    add_dependencies(${target_name} tgfx_prebuilt_mac)
  endfunction()

  function(motionstudio_link_prebuilt_tgfx target_name)
    target_link_libraries(${target_name} PUBLIC "${TGFX_PREBUILT_MAC_LIB}")
  endfunction()
endif()

add_files_by_extension(TGFX_ADAPTER_SOURCES ".h;.mm;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_files_by_extension(TGFX_ADAPTER_PUBLIC_HEADERS ".h"
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(tgfx_adapter STATIC ${TGFX_ADAPTER_PUBLIC_HEADERS} ${TGFX_ADAPTER_SOURCES})
set_target_properties(tgfx_adapter PROPERTIES OUTPUT_NAME motionstudio_tgfx_adapter)

add_library(motionstudio_tgfx_adapter ALIAS tgfx_adapter)
add_source_group(TGFX_ADAPTER_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src "Sources")

target_include_directories(tgfx_adapter
                           PUBLIC
                           "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_include_directories(tgfx_adapter
                           PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}/src"
                           "${TGFX_INCLUDE_DIR}"
                           "${PROJECT_SOURCE_DIR}/adapter/textlayout/include")

target_link_libraries(tgfx_adapter
                      PUBLIC
                      core
                      textlayout
                      "-framework Metal"
                      "-framework MetalKit"
                      "-framework Foundation"
                      "-framework QuartzCore"
                      "-framework CoreMedia"
                      "-framework CoreVideo"
                      "-framework ImageIO"
                      "-framework CoreGraphics"
                      "-framework CoreText"
                      iconv)

# Xcode / gen_mac: Mac destination is Catalyst (UIKit). Native mac Cocoa is Ninja-only.
if(NOT CMAKE_GENERATOR MATCHES "Xcode")
  target_link_libraries(tgfx_adapter
                        PUBLIC
                        "-framework ApplicationServices"
                        "-framework Cocoa")
endif()

motionstudio_add_tgfx_prebuild(tgfx_adapter)
motionstudio_link_prebuilt_tgfx(tgfx_adapter)

include(GoogleTest)
add_files_by_extension(TGFX_ADAPTER_TEST_SOURCES ".h;.cpp;.mm"
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

add_executable(tgfx_adapter_test ${TGFX_ADAPTER_TEST_SOURCES})
add_source_group(TGFX_ADAPTER_TEST_SOURCES ${CMAKE_CURRENT_LIST_DIR}/tests "Tests")

disable_xcode_target_signing(tgfx_adapter_test)

target_link_libraries(tgfx_adapter_test PRIVATE motionstudio_tgfx_adapter GTest::gtest_main)
target_include_directories(tgfx_adapter_test
                           PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}/src"
                           "${TGFX_INCLUDE_DIR}")
motionstudio_add_tgfx_prebuild(tgfx_adapter_test)
motionstudio_link_prebuilt_tgfx(tgfx_adapter_test)
gtest_discover_tests(tgfx_adapter_test)
