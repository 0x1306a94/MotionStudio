# Build tgfx for the current Xcode destination (or macosx by default).
#
# Expected -D cache vars from the caller:
#   TGFX_SOURCE_DIR  — path to third_party/tgfx
#   TGFX_OUT_DIR     — root output directory; layout: <out>/<CMAKE_BUILD_TYPE>/<mac|ios>/<arch>/
#   TGFX_CMAKE_ARGS  — extra -D flags passed to build_tgfx (space separated)
#   TGFX_MACOSX_DEPLOYMENT_TARGET   — used for macosx
#   TGFX_IPHONEOS_DEPLOYMENT_TARGET — used for iphoneos / iphonesimulator
#   CMAKE_BUILD_TYPE                — forwarded to build_tgfx; Debug → also pass -d
#
# Reads from the environment when present (Xcode script phase):
#   PLATFORM_NAME    — macosx / iphoneos / iphonesimulator

if(NOT TGFX_SOURCE_DIR OR NOT TGFX_OUT_DIR)
  message(FATAL_ERROR "BuildTgfx.cmake requires TGFX_SOURCE_DIR and TGFX_OUT_DIR")
endif()

find_program(NODE_EXECUTABLE NAMES node REQUIRED)

if(DEFINED ENV{PLATFORM_NAME} AND NOT "$ENV{PLATFORM_NAME}" STREQUAL "")
  set(_platform_name "$ENV{PLATFORM_NAME}")
else()
  set(_platform_name "macosx")
endif()

if(_platform_name STREQUAL "macosx")
  set(_tgfx_platform "mac")
  set(_tgfx_arch "arm64")
  set(_tgfx_out_subdir "mac")
  if(NOT TGFX_MACOSX_DEPLOYMENT_TARGET)
    message(FATAL_ERROR "BuildTgfx: TGFX_MACOSX_DEPLOYMENT_TARGET is required for macosx")
  endif()
  set(_deploy_target "${TGFX_MACOSX_DEPLOYMENT_TARGET}")
elseif(_platform_name STREQUAL "iphoneos")
  set(_tgfx_platform "ios")
  set(_tgfx_arch "arm64")
  set(_tgfx_out_subdir "ios")
  if(NOT TGFX_IPHONEOS_DEPLOYMENT_TARGET)
    message(FATAL_ERROR "BuildTgfx: TGFX_IPHONEOS_DEPLOYMENT_TARGET is required for iphoneos")
  endif()
  set(_deploy_target "${TGFX_IPHONEOS_DEPLOYMENT_TARGET}")
elseif(_platform_name STREQUAL "iphonesimulator")
  set(_tgfx_platform "ios")
  set(_tgfx_arch "arm64-simulator")
  set(_tgfx_out_subdir "ios")
  if(NOT TGFX_IPHONEOS_DEPLOYMENT_TARGET)
    message(FATAL_ERROR "BuildTgfx: TGFX_IPHONEOS_DEPLOYMENT_TARGET is required for iphonesimulator")
  endif()
  set(_deploy_target "${TGFX_IPHONEOS_DEPLOYMENT_TARGET}")
else()
  message(FATAL_ERROR "BuildTgfx: unsupported PLATFORM_NAME='${_platform_name}'")
endif()

if(NOT CMAKE_BUILD_TYPE)
  if(DEFINED ENV{CONFIGURATION} AND NOT "$ENV{CONFIGURATION}" STREQUAL "")
    set(CMAKE_BUILD_TYPE "$ENV{CONFIGURATION}")
  else()
    set(CMAKE_BUILD_TYPE "Release")
  endif()
endif()

set(_debug_flag "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_debug_flag "-d")
endif()

set(_out_root "${TGFX_OUT_DIR}/${CMAKE_BUILD_TYPE}/${_tgfx_out_subdir}")
set(_arch_dir "${_out_root}/${_tgfx_arch}")

set(_cmake_args "")
if(TGFX_CMAKE_ARGS)
  separate_arguments(_cmake_args NATIVE_COMMAND "${TGFX_CMAKE_ARGS}")
endif()
# tgfx vendor ios-cmake reads DEPLOYMENT_TARGET; also set CMAKE_OSX_DEPLOYMENT_TARGET.
list(APPEND _cmake_args
     "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
     "-DCMAKE_OSX_DEPLOYMENT_TARGET=${_deploy_target}"
     "-DDEPLOYMENT_TARGET=${_deploy_target}")

message(STATUS "BuildTgfx: platform=${_platform_name} CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -> -p ${_tgfx_platform} -a ${_tgfx_arch} -o ${_out_root} deploy=${_deploy_target}")

execute_process(
  COMMAND
    "${NODE_EXECUTABLE}" "${TGFX_SOURCE_DIR}/build_tgfx" tgfx
    -p "${_tgfx_platform}"
    -a "${_tgfx_arch}"
    -o "${_out_root}"
    -i
    ${_debug_flag}
    ${_cmake_args}
  WORKING_DIRECTORY "${TGFX_SOURCE_DIR}"
  RESULT_VARIABLE _tgfx_build_result
)
if(NOT _tgfx_build_result EQUAL 0)
  message(FATAL_ERROR "BuildTgfx: build_tgfx failed with code ${_tgfx_build_result}")
endif()

if(NOT EXISTS "${_arch_dir}/tgfx.a")
  message(FATAL_ERROR "BuildTgfx: expected library missing: ${_arch_dir}/tgfx.a")
endif()

# build_tgfx emits tgfx.a; expose libtgfx.a so -ltgfx works with LIBRARY_SEARCH_PATHS.
if(NOT EXISTS "${_arch_dir}/libtgfx.a")
  execute_process(
    COMMAND "${CMAKE_COMMAND}" -E create_symlink tgfx.a libtgfx.a
    WORKING_DIRECTORY "${_arch_dir}"
    RESULT_VARIABLE _symlink_result
  )
  if(NOT _symlink_result EQUAL 0)
    message(FATAL_ERROR "BuildTgfx: failed to create libtgfx.a symlink in ${_arch_dir}")
  endif()
endif()
