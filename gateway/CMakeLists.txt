cmake_minimum_required(VERSION 3.20)
project(ugvc_gateway LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # cppcheck, clangd용

option(UGVC_BUILD_TESTS "Build GoogleTest unit tests" ON)

# 우리 코드에만 적용할 경고 옵션 (GoogleTest 같은 외부 코드에는 적용하지 않음)
add_library(ugvc_warnings INTERFACE)
target_compile_options(ugvc_warnings INTERFACE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Werror>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>
)

# codec: 바이트 직렬화, CRC16, 헤더 encode/decode (ICD 3~4장)
add_library(ugvc_codec
  src/codec/crc16.cpp
)
target_include_directories(ugvc_codec PUBLIC include)
target_link_libraries(ugvc_codec PRIVATE ugvc_warnings)

if(UGVC_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
  )
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # Windows MSVC 런타임 일치
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(ugvc_tests
    tests/codec/crc16_test.cpp
  )
  target_link_libraries(ugvc_tests PRIVATE ugvc_codec ugvc_warnings GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(ugvc_tests)
endif()
