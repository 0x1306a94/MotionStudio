# AVFoundation H.264 / MP4 encoder adapter (Apple only).

add_files_by_extension(AVF_ADAPTER_SOURCES ".h;.mm;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_files_by_extension(AVF_ADAPTER_PUBLIC_HEADERS ".h"
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(avf_adapter STATIC ${AVF_ADAPTER_PUBLIC_HEADERS} ${AVF_ADAPTER_SOURCES})
set_target_properties(avf_adapter PROPERTIES OUTPUT_NAME motionstudio_avf_adapter)
add_library(motionstudio_avf_adapter ALIAS avf_adapter)

add_source_group(AVF_ADAPTER_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src "Sources")
add_source_group(AVF_ADAPTER_PUBLIC_HEADERS ${CMAKE_CURRENT_LIST_DIR}/include "Headers")

target_include_directories(avf_adapter
                           PUBLIC
                           "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(avf_adapter
                      PUBLIC
                      core
                      "-framework AVFoundation"
                      "-framework CoreMedia"
                      "-framework CoreVideo"
                      "-framework Foundation"
                      "-framework QuartzCore")

target_compile_options(avf_adapter PRIVATE -Wall -Wextra)

if(CMAKE_GENERATOR MATCHES "Xcode")
  set_target_properties(avf_adapter PROPERTIES
    XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator macosx"
  )
endif()

include(GoogleTest)
add_files_by_extension(AVF_ADAPTER_TEST_SOURCES ".h;.mm;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

add_executable(avf_adapter_test ${AVF_ADAPTER_TEST_SOURCES})
add_source_group(AVF_ADAPTER_TEST_SOURCES ${CMAKE_CURRENT_LIST_DIR}/tests "Tests")
disable_xcode_target_signing(avf_adapter_test)

target_link_libraries(avf_adapter_test PRIVATE
                      motionstudio_avf_adapter
                      motionstudio_tgfx_adapter
                      GTest::gtest_main)
target_include_directories(avf_adapter_test
                           PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}/include"
                           "${PROJECT_SOURCE_DIR}/adapter/tgfx/include"
                           "${PROJECT_SOURCE_DIR}/third_party/tgfx/include")
motionstudio_add_tgfx_prebuild(avf_adapter_test)
motionstudio_link_prebuilt_tgfx(avf_adapter_test)
gtest_discover_tests(avf_adapter_test)
