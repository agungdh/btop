# SPDX-License-Identifier: Apache-2.0

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
  FIND_PACKAGE_ARGS NAMES GTest
)
FetchContent_MakeAvailable(googletest)

add_library(libbtop_test)
target_include_directories(libbtop_test PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(libbtop_test libbtop GTest::gtest_main)

add_executable(btop_test cpu_names.cpp tools.cpp json_test.cpp)
target_link_libraries(btop_test libbtop_test)

include(GoogleTest)
gtest_discover_tests(btop_test)

# Integration smoke test: spawns the real btop binary in HTTP server mode on an
# ephemeral port and checks the endpoints. Requires curl and a working /proc.
add_test(NAME smoke_http COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/smoke_http.sh $<TARGET_FILE:btop>)
set_tests_properties(smoke_http PROPERTIES TIMEOUT 60)
