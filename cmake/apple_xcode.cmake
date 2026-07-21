# Multi-destination Apple Xcode settings (SDKROOT=auto).
# Include only when CMAKE_GENERATOR is Xcode. No toolchain file required.
#
# Required cache options (per platform present in MOTIONSTUDIO_SUPPORTED_PLATFORMS):
#   MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET
#   MOTIONSTUDIO_IPHONEOS_DEPLOYMENT_TARGET
#   MOTIONSTUDIO_TVOS_DEPLOYMENT_TARGET
#   MOTIONSTUDIO_WATCHOS_DEPLOYMENT_TARGET
#   MOTIONSTUDIO_XROS_DEPLOYMENT_TARGET
#
# Other:
#   MOTIONSTUDIO_SUPPORTED_PLATFORMS  — space-separated Xcode SUPPORTED_PLATFORMS
#   MOTIONSTUDIO_ENABLE_MACCATALYST   — SUPPORTS_MACCATALYST

if(NOT CMAKE_GENERATOR MATCHES "Xcode")
  return()
endif()

if(NOT DEFINED MOTIONSTUDIO_SUPPORTED_PLATFORMS)
  set(MOTIONSTUDIO_SUPPORTED_PLATFORMS
          "macosx iphoneos iphonesimulator appletvos appletvsimulator watchos watchsimulator xros xrsimulator")
endif()
set(MOTIONSTUDIO_SUPPORTED_PLATFORMS "${MOTIONSTUDIO_SUPPORTED_PLATFORMS}" CACHE STRING
        "Space-separated Xcode SUPPORTED_PLATFORMS")

option(MOTIONSTUDIO_ENABLE_MACCATALYST "Enable Mac Catalyst (SUPPORTS_MACCATALYST)" ON)

set(_ms_platforms " ${MOTIONSTUDIO_SUPPORTED_PLATFORMS} ")

macro(_ms_require_deployment_target cache_var platform_keywords)
  set(_needed FALSE)
  foreach(_keyword ${platform_keywords})
    if(_ms_platforms MATCHES " ${_keyword} ")
      set(_needed TRUE)
    endif()
  endforeach()
  if(_needed)
    if(NOT ${cache_var})
      message(FATAL_ERROR
              "${cache_var} must be set explicitly when MOTIONSTUDIO_SUPPORTED_PLATFORMS includes: ${platform_keywords}")
    endif()
    set(${cache_var} "${${cache_var}}" CACHE STRING "${cache_var}" FORCE)
  endif()
endmacro()

_ms_require_deployment_target(MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET "macosx")
_ms_require_deployment_target(MOTIONSTUDIO_IPHONEOS_DEPLOYMENT_TARGET "iphoneos;iphonesimulator")
_ms_require_deployment_target(MOTIONSTUDIO_TVOS_DEPLOYMENT_TARGET "appletvos;appletvsimulator")
_ms_require_deployment_target(MOTIONSTUDIO_WATCHOS_DEPLOYMENT_TARGET "watchos;watchsimulator")
_ms_require_deployment_target(MOTIONSTUDIO_XROS_DEPLOYMENT_TARGET "xros;xrsimulator")

# Keep CMake's macOS deployment var in sync with the explicit macOS target only.
if(MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "${MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET}" CACHE STRING
          "Minimum macOS deployment version" FORCE)
endif()

set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Apple architectures" FORCE)

set(CMAKE_XCODE_ATTRIBUTE_SDKROOT "auto")
set(CMAKE_XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "${MOTIONSTUDIO_SUPPORTED_PLATFORMS}")
set(CMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "YES")
set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS "arm64")
set(CMAKE_XCODE_ATTRIBUTE_VALID_ARCHS "arm64")

if(MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET)
  set(CMAKE_XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "${MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET}")
endif()
if(MOTIONSTUDIO_IPHONEOS_DEPLOYMENT_TARGET)
  set(CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${MOTIONSTUDIO_IPHONEOS_DEPLOYMENT_TARGET}")
endif()
if(MOTIONSTUDIO_TVOS_DEPLOYMENT_TARGET)
  set(CMAKE_XCODE_ATTRIBUTE_TVOS_DEPLOYMENT_TARGET "${MOTIONSTUDIO_TVOS_DEPLOYMENT_TARGET}")
endif()
if(MOTIONSTUDIO_WATCHOS_DEPLOYMENT_TARGET)
  set(CMAKE_XCODE_ATTRIBUTE_WATCHOS_DEPLOYMENT_TARGET "${MOTIONSTUDIO_WATCHOS_DEPLOYMENT_TARGET}")
endif()
if(MOTIONSTUDIO_XROS_DEPLOYMENT_TARGET)
  set(CMAKE_XCODE_ATTRIBUTE_XROS_DEPLOYMENT_TARGET "${MOTIONSTUDIO_XROS_DEPLOYMENT_TARGET}")
endif()

if(MOTIONSTUDIO_ENABLE_MACCATALYST)
  set(CMAKE_XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST "YES")
else()
  set(CMAKE_XCODE_ATTRIBUTE_SUPPORTS_MACCATALYST "NO")
endif()

set(CMAKE_XCODE_ATTRIBUTE_ARCHS[sdk=macosx*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_VALID_ARCHS[sdk=macosx*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS[sdk=iphoneos*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_VALID_ARCHS[sdk=iphoneos*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS[sdk=iphonesimulator*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_VALID_ARCHS[sdk=iphonesimulator*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS[sdk=appletvos*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_VALID_ARCHS[sdk=appletvos*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS[sdk=appletvsimulator*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_VALID_ARCHS[sdk=appletvsimulator*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS[sdk=watchos*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_VALID_ARCHS[sdk=watchos*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS[sdk=watchsimulator*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_VALID_ARCHS[sdk=watchsimulator*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS[sdk=xros*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_VALID_ARCHS[sdk=xros*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_ARCHS[sdk=xrsimulator*] "arm64")
set(CMAKE_XCODE_ATTRIBUTE_VALID_ARCHS[sdk=xrsimulator*] "arm64")

message(STATUS "Apple Xcode multi-destination: SDKROOT=auto, platforms=${MOTIONSTUDIO_SUPPORTED_PLATFORMS}, arch=arm64")
