# tgfx-based render adapter (Metal backend), Apple platforms only.
# tgfx is prebuilt per Destination via build_tgfx (not add_subdirectory).

set(TGFX_SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/tgfx")
set(TGFX_OUT_DIR "${CMAKE_BINARY_DIR}/tgfx_prebuilt")
set(TGFX_INCLUDE_DIR "${TGFX_SOURCE_DIR}/include")

set(TGFX_CMAKE_ARGS "-DTGFX_USE_METAL=ON -DTGFX_USE_OPENGL=OFF -DTGFX_USE_PNG_DECODE=ON -DTGFX_USE_PNG_ENCODE=ON -DTGFX_USE_JPEG_DECODE=ON -DTGFX_USE_JPEG_ENCODE=ON -DTGFX_USE_WEBP_DECODE=ON -DTGFX_USE_WEBP_ENCODE=ON")

set(TGFX_PREBUILT_MAC_DIR "${TGFX_OUT_DIR}/$(CONFIGURATION)/mac/arm64")
set(TGFX_PREBUILT_IOS_DIR "${TGFX_OUT_DIR}/$(CONFIGURATION)/ios/arm64")
set(TGFX_PREBUILT_IOSSIM_DIR "${TGFX_OUT_DIR}/$(CONFIGURATION)/ios/arm64-simulator")

if(NOT MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET)
  message(FATAL_ERROR "adapter/tgfx requires MOTIONSTUDIO_MACOSX_DEPLOYMENT_TARGET")
endif()
if(NOT MOTIONSTUDIO_IPHONEOS_DEPLOYMENT_TARGET)
  message(FATAL_ERROR "adapter/tgfx requires MOTIONSTUDIO_IPHONEOS_DEPLOYMENT_TARGET")
endif()

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
  if(CMAKE_GENERATOR MATCHES "Xcode")
    set_target_properties(${target_name} PROPERTIES
        XCODE_ATTRIBUTE_LIBRARY_SEARCH_PATHS[sdk=macosx*] "${TGFX_PREBUILT_MAC_DIR}"
        XCODE_ATTRIBUTE_LIBRARY_SEARCH_PATHS[sdk=iphoneos*] "${TGFX_PREBUILT_IOS_DIR}"
        XCODE_ATTRIBUTE_LIBRARY_SEARCH_PATHS[sdk=iphonesimulator*] "${TGFX_PREBUILT_IOSSIM_DIR}")
    target_link_libraries(${target_name} PUBLIC "-ltgfx")
  else()
    motionstudio_add_tgfx_prebuild(${target_name})
    target_link_libraries(${target_name} PUBLIC
                          "${TGFX_OUT_DIR}/${CMAKE_BUILD_TYPE}/mac/arm64/tgfx.a")
  endif()
endfunction()

add_library(motionstudio_tgfx_adapter STATIC TgfxRenderAdapter.mm)
target_include_directories(motionstudio_tgfx_adapter
                           PUBLIC
                           "${CMAKE_CURRENT_SOURCE_DIR}"
                           "${TGFX_INCLUDE_DIR}")
target_link_libraries(motionstudio_tgfx_adapter
                      PUBLIC core "-framework Metal" "-framework Foundation")

if(CMAKE_GENERATOR MATCHES "Xcode")
  motionstudio_add_tgfx_prebuild(motionstudio_tgfx_adapter)
endif()
motionstudio_link_prebuilt_tgfx(motionstudio_tgfx_adapter)

include(GoogleTest)
add_executable(tgfx_adapter_test TgfxRenderAdapterTest.cpp)
target_link_libraries(tgfx_adapter_test
                      PRIVATE motionstudio_tgfx_adapter GTest::gtest_main)
if(CMAKE_GENERATOR MATCHES "Xcode")
  motionstudio_add_tgfx_prebuild(tgfx_adapter_test)
  motionstudio_link_prebuilt_tgfx(tgfx_adapter_test)
endif()
gtest_discover_tests(tgfx_adapter_test)
