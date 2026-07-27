include(GoogleTest)


add_files_by_extension(COMMON_TESTS_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/common
)

add_files_by_extension(CORE_TESTS_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/core
)

add_files_by_extension(MODEL_TESTS_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/model
)

add_files_by_extension(SERIALIZATION_TESTS_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/serialization
)

add_files_by_extension(RENDER_TESTS_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/render
)

add_files_by_extension(ANIMATION_TESTS_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/animation
)

add_files_by_extension(UNDO_TESTS_SOURCES ".h;.cpp"
    ${CMAKE_CURRENT_SOURCE_DIR}/undo
)

list(APPEND TESTS_SOURCES
    ${COMMON_TESTS_SOURCES}
    ${CORE_TESTS_SOURCES}
    ${MODEL_TESTS_SOURCES}
    ${SERIALIZATION_TESTS_SOURCES}
    ${RENDER_TESTS_SOURCES}
    ${ANIMATION_TESTS_SOURCES}
    ${UNDO_TESTS_SOURCES}
)
add_executable(core_tests ${TESTS_SOURCES})
add_source_group(TESTS_SOURCES ${CMAKE_CURRENT_LIST_DIR} "Sources")

disable_xcode_target_signing(core_tests)

if(CMAKE_GENERATOR MATCHES "Xcode")
  set_target_properties(core_tests PROPERTIES
    XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator macosx"
  )
  set_target_properties(gtest PROPERTIES
    XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator macosx"
  )
  set_target_properties(gtest_main PROPERTIES
    XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator macosx"
  )
  set_target_properties(gmock PROPERTIES
    XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator macosx"
  )
  set_target_properties(gmock_main PROPERTIES
    XCODE_ATTRIBUTE_SUPPORTED_PLATFORMS "iphoneos iphonesimulator macosx"
  )
endif()

# nlohmann/json 仅测试用来校验 JSON 产物结构，非 core 公共依赖。
target_link_libraries(core_tests PRIVATE core GTest::gtest_main nlohmann_json::nlohmann_json)
gtest_discover_tests(core_tests)
