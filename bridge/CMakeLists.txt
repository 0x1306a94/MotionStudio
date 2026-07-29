add_files_by_extension(COMMON_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
)

add_files_by_extension(BRIDGE_PUBLIC_HEADERS ".h"
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

list(APPEND BRIDGE_ALL_SOURCES ${COMMON_SOURCES})
list(APPEND BRIDGE_LINK_LIBRARIES core)

# Canvas (MTKView direct rendering) is Apple-only.
if (APPLE)
    add_files_by_extension(APPLE_SOURCES ".h;.mm"
        ${CMAKE_CURRENT_SOURCE_DIR}/src/apple
    )
    list(APPEND BRIDGE_ALL_SOURCES ${APPLE_SOURCES})
    list(APPEND BRIDGE_LINK_LIBRARIES motionstudio_tgfx_adapter)
endif()

message(STATUS "BRIDGE_PUBLIC_HEADERS: ${BRIDGE_PUBLIC_HEADERS}")
message(STATUS "BRIDGE_ALL_SOURCES: ${BRIDGE_ALL_SOURCES}")
message(STATUS "BRIDGE_LINK_LIBRARIES: ${BRIDGE_LINK_LIBRARIES}")

add_library(bridge STATIC ${BRIDGE_PUBLIC_HEADERS} ${BRIDGE_ALL_SOURCES})
set_target_properties(bridge PROPERTIES OUTPUT_NAME motionstudio_bridge)

add_library(motionstudio_bridge ALIAS bridge)

add_source_group(BRIDGE_ALL_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src "Sources")
add_source_group(BRIDGE_PUBLIC_HEADERS ${CMAKE_CURRENT_LIST_DIR}/include "Headers")

target_include_directories(bridge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(bridge PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(bridge PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../adapter/tgfx/src")
target_link_libraries(bridge PUBLIC ${BRIDGE_LINK_LIBRARIES})
target_compile_options(bridge PRIVATE -Wall -Wextra)


include(GoogleTest)

add_files_by_extension(BRIDGE_TEST_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

add_executable(bridge_test ${BRIDGE_TEST_SOURCES})
add_source_group(BRIDGE_TEST_SOURCES ${CMAKE_CURRENT_LIST_DIR}/tests "Tests")

set_xcode_target_tgfx_prebuilt_search_paths(bridge_test)
disable_xcode_target_signing(bridge_test)

target_include_directories(bridge_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/common")
target_link_libraries(bridge_test PRIVATE bridge GTest::gtest_main)
gtest_discover_tests(bridge_test)
