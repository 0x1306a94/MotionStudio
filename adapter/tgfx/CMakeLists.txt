# tgfx-based render adapter (Metal backend), Apple platforms only.

set(TGFX_USE_METAL ON CACHE BOOL "" FORCE)
set(TGFX_USE_OPENGL OFF CACHE BOOL "" FORCE)
set(TGFX_USE_PNG_DECODE OFF CACHE BOOL "" FORCE)
set(TGFX_USE_PNG_ENCODE OFF CACHE BOOL "" FORCE)
set(TGFX_USE_JPEG_DECODE OFF CACHE BOOL "" FORCE)
set(TGFX_USE_JPEG_ENCODE OFF CACHE BOOL "" FORCE)
set(TGFX_USE_WEBP_DECODE OFF CACHE BOOL "" FORCE)
set(TGFX_USE_WEBP_ENCODE OFF CACHE BOOL "" FORCE)

add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/tgfx
                 ${CMAKE_BINARY_DIR}/tgfx EXCLUDE_FROM_ALL)

add_library(motionstudio_tgfx_adapter STATIC TgfxRenderAdapter.mm)
target_include_directories(motionstudio_tgfx_adapter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(motionstudio_tgfx_adapter
                      PUBLIC core tgfx "-framework Metal" "-framework Foundation")

include(GoogleTest)
add_executable(tgfx_adapter_test TgfxRenderAdapterTest.cpp)
target_link_libraries(tgfx_adapter_test
                      PRIVATE motionstudio_tgfx_adapter GTest::gtest_main)
gtest_discover_tests(tgfx_adapter_test)
