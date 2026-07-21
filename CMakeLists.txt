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

enable_testing()
add_subdirectory(tests)
add_subdirectory(benchmarks)

if (APPLE)
    add_subdirectory(adapter/tgfx)
endif()

# C ABI bridge for platform UI shells; must come after adapter/tgfx on Apple
# (the canvas API links the tgfx adapter).
add_subdirectory(bridge)
