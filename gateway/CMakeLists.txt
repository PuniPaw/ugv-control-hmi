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
  src/codec/frame.cpp
  src/codec/telemetry.cpp
  src/codec/detection.cpp
  src/codec/ack.cpp
)
target_include_directories(ugvc_codec PUBLIC include)
target_link_libraries(ugvc_codec PRIVATE ugvc_warnings)

include(FetchContent)

# nlohmann/json: IF-03 JSON 생성, 시험 벡터 로드
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
# standalone Asio (헤더 전용, CMake 없음 → 소스만 받아 include 경로로 사용)
FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG        asio-1-30-2
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json asio)

find_package(Threads REQUIRED)
add_library(ugvc_asio INTERFACE)
# SYSTEM: 외부 헤더의 경고는 -Werror 대상에서 제외
target_include_directories(ugvc_asio SYSTEM INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(ugvc_asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(ugvc_asio INTERFACE Threads::Threads)

# ingress: 수신 검증 규칙 ①~⑨, 원인별 계수 (ICD 9장)
add_library(ugvc_ingress
  src/ingress/validator.cpp
)
target_include_directories(ugvc_ingress PUBLIC include)
target_link_libraries(ugvc_ingress PUBLIC ugvc_codec PRIVATE ugvc_warnings)

# gateway: 수신 처리 중심(Gateway), 링크 상태, 유실률, HEARTBEAT, IF-03 JSON. 소켓 없음 → 단위 시험 대상
add_library(ugvc_gateway_core
  src/gateway/gateway.cpp
  src/if03/messages.cpp
)
target_include_directories(ugvc_gateway_core PUBLIC include)
target_link_libraries(ugvc_gateway_core PUBLIC ugvc_ingress nlohmann_json::nlohmann_json PRIVATE ugvc_warnings)

# GW 실행 파일: 소켓·타이머만 담당
# app_io: 출력 전용 스레드가 쓰는 줄 단위 출력(LineSink). io 스레드를 막지 않는다
add_library(ugvc_app_io src/app/line_sink.cpp)
target_include_directories(ugvc_app_io PUBLIC src/app)
target_link_libraries(ugvc_app_io PUBLIC Threads::Threads PRIVATE ugvc_warnings)

add_executable(ugvc_gateway src/app/main.cpp)
target_link_libraries(ugvc_gateway PRIVATE ugvc_gateway_core ugvc_app_io ugvc_asio ugvc_warnings)

if(UGVC_BUILD_TESTS)
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
    tests/codec/bytes_test.cpp
    tests/codec/crc16_test.cpp
    tests/codec/frame_test.cpp
    tests/codec/telemetry_test.cpp
    tests/codec/detection_ack_test.cpp
    tests/ingress/validator_test.cpp
    tests/ingress/seq_tracker_test.cpp
    tests/link/link_monitor_test.cpp
    tests/link/loss_window_test.cpp
    tests/gateway/gateway_test.cpp
    tests/gateway/heartbeat_test.cpp
    tests/core/periodic_test.cpp
    tests/if03/messages_test.cpp
    tests/app/line_sink_test.cpp
  )
  target_include_directories(ugvc_tests PRIVATE tests)
  target_link_libraries(ugvc_tests PRIVATE
    ugvc_gateway_core ugvc_ingress ugvc_codec ugvc_app_io ugvc_warnings GTest::gtest_main nlohmann_json::nlohmann_json)
  # 공용 테스트 벡터 위치 (STP 2.4)
  target_compile_definitions(ugvc_tests PRIVATE
    UGVC_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../testdata")

  include(GoogleTest)
  gtest_discover_tests(ugvc_tests)
endif()
