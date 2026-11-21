cmake_minimum_required(VERSION 3.24)
project(lumen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(LUMEN_BUILD_TESTS "Build unit tests and register the program suite" ON)
# e.g. -DLUMEN_SANITIZERS=address,undefined
set(LUMEN_SANITIZERS "" CACHE STRING "Comma-separated -fsanitize= list")

if(LUMEN_SANITIZERS)
  add_compile_options(-fsanitize=${LUMEN_SANITIZERS} -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${LUMEN_SANITIZERS})
endif()

set(LUMEN_WARNINGS -Wall -Wextra -Wpedantic -Werror)
find_package(Threads REQUIRED)

add_library(lumen_core
  src/lexer.cpp
  src/parser.cpp
  src/value.cpp
  src/interpreter.cpp
  src/builtins.cpp)
target_include_directories(lumen_core PUBLIC include)
target_compile_options(lumen_core PRIVATE ${LUMEN_WARNINGS})

add_executable(lumen tools/lumen.cpp)
target_link_libraries(lumen PRIVATE lumen_core Threads::Threads)
target_compile_options(lumen PRIVATE ${LUMEN_WARNINGS})

if(LUMEN_BUILD_TESTS)
  include(FetchContent)
  # Vendored tarball when present (offline machines), else GitHub; same hash.
  set(_gtest_tarball ${CMAKE_CURRENT_SOURCE_DIR}/third_party/googletest-1.15.2.tar.gz)
  if(EXISTS ${_gtest_tarball})
    set(_gtest_url ${_gtest_tarball})
  else()
    set(_gtest_url https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz)
  endif()
  FetchContent_Declare(googletest
    URL ${_gtest_url}
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(lumen_tests tests/lexer_test.cpp tests/parser_test.cpp tests/interpreter_test.cpp)
  target_link_libraries(lumen_tests PRIVATE lumen_core GTest::gtest_main)
  target_compile_options(lumen_tests PRIVATE ${LUMEN_WARNINGS})
  include(GoogleTest)
  gtest_discover_tests(lumen_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 60)

  # The language-level suite: every tests/programs/*.lm against its
  # // expect: comments.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME programs
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/run_tests.py
                     --lumen $<TARGET_FILE:lumen> ${CMAKE_CURRENT_SOURCE_DIR}/tests/programs)
  endif()
endif()
