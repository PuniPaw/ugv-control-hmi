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
FetchContent_MakeAvailable(nlohmann_json)

# Boost.Asio(UDP·타이머)와 Boost.Beast(WebSocket, ADR-003). 헤더 전용 부분만 쓴다.
# 시스템 패키지(Ubuntu: libboost-dev)로 설치한다. Boost 전체를 FetchContent로 받으면 빌드가 너무 길다.
find_package(Threads REQUIRED)
find_package(Boost 1.83 REQUIRED CONFIG)
add_library(ugvc_asio INTERFACE)
target_link_libraries(ugvc_asio INTERFACE Boost::headers Threads::Threads)
target_compile_definitions(ugvc_asio INTERFACE BOOST_ASIO_NO_DEPRECATED)

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
add_library(ugvc_app_io src/app/line_sink.cpp src/app/ws_server.cpp)
target_include_directories(ugvc_app_io PUBLIC src/app)
target_link_libraries(ugvc_app_io PUBLIC ugvc_asio Threads::Threads PRIVATE ugvc_warnings)

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
