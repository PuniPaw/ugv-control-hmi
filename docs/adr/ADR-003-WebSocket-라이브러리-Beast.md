# ADR-003. GW의 WebSocket(IF-03)은 Boost.Beast로 구현하고, GW의 Asio를 Boost.Asio로 바꾼다

| 항목 | 내용 |
|---|---|
| 상태 | 승인 |
| 날짜 | 2026-11-11 |
| 관련 | UGVC-ICD-001 IF-03(7장), UGVC-SDD-001 4장, REQ-COM-005, REQ-HMI-002, REQ-SAFE-001 |

## 배경

- GW는 IF-03(WebSocket, JSON)으로 HMI에 텔레메트리·탐지·stats·link_status를 보내고, HMI의 명령을 받아야 한다.
- GW는 단일 스레드 Asio `io_context` 하나로 UDP 수신, 50ms 판정 타이머, HEARTBEAT 1Hz를 처리한다. 판정 로직(`Gateway`)은 이 스레드에서만 접근하므로 잠금이 없다.
- 지금까지 쓴 Asio는 **standalone Asio**(`asio::`)다. 후보 라이브러리는 대부분 **Boost.Asio**(`boost::asio::`)를 전제로 한다.
- 리뷰에서 stdout이 막히자 단일 스레드 전체가 멈춰 HEARTBEAT가 끊긴 일이 있었다 (PR #7). 같은 일이 느린 WebSocket 클라이언트로 반복되면 안 된다.

## 선택지와 시제품 결과

세 후보에 같은 과제를 주었다: 단일 스레드에서 100ms마다 JSON을 모든 클라이언트에 브로드캐스트하고, 클라이언트의 텍스트 메시지를 받는다. Python `websockets` 클라이언트 2개로 1초 동안 확인했다.

| | A. Boost.Beast | B. uWebSockets 20.70 | C. websocketpp 0.8.2 |
|---|---|---|---|
| 시제품 동작 | 클라이언트당 11개, 간격 100ms, 명령 수신 | 클라이언트당 11개, 간격 100ms, 명령 수신 | GW 설정(`ASIO_NO_DEPRECATED`)에서 **컴파일 실패** (`io_service` 사용) |
| 이벤트 루프 | GW와 같은 `io_context` | 자체 루프(epoll). Asio 백엔드는 Boost.Asio 필요 | standalone Asio 가능 |
| GW에 붙이는 방법 | Asio를 Boost.Asio로 바꾼다 | 루프 2개를 스레드로 나누거나(공유 상태 동기화 필요), 한 루프가 다른 루프를 주기적으로 poll | (해당 없음) |
| 의존성 | Boost 헤더 (Ubuntu `libboost-dev` 1.83, 헤더 전용) | uSockets C 라이브러리 빌드, zlib | 헤더 전용 |
| 시제품 빌드 시간 / 실행 파일 | 13.8s / 1.26MB (`-O2`) | 3.6s / 173KB | — |
| 유지보수 | Boost 정기 릴리스 | 활발 | 마지막 릴리스 2020-04 |
| 느린 클라이언트 대처 | 세션별 쓰기 대기열을 직접 둔다 (시제품: 1000개 상한) | 라이브러리 내장 backpressure | — |

A를 고를 때 GW를 Boost.Asio로 바꾸는 비용도 GW 사본에서 실제로 쟀다. 바뀐 파일은 `app/main.cpp`, `app/line_output.hpp` 2개(약 13줄: include, `namespace asio = boost::asio`, `error_code` 타입, POSIX 매크로 이름)와 CMake의 Asio 대상이다. 단위 시험 79개와 통합 시험 12개가 그대로 통과했다. 판정 로직(`codec`·`ingress`·`link`·`gateway`)은 Asio를 쓰지 않으므로 바뀌지 않는다.

## 결정

**A를 채택한다.** IF-03은 Boost.Beast로 구현하고, GW의 Asio는 standalone Asio에서 Boost.Asio로 바꾼다.

- WebSocket 세션도 기존 `io_context` 한 스레드에서 돈다. `Gateway`에 잠금을 넣지 않는다.
- 세션마다 쓰기 대기열에 상한을 둔다. 넘치면 그 클라이언트의 메시지만 버리고 계수한다 (`LineOutput`과 같은 정책). 느린 HMI 하나가 판정 주기와 HEARTBEAT를 멈추지 못하게 한다.
- Boost는 시스템 패키지(`libboost-dev`, 1.74 이상)를 `find_package(Boost)`로 찾는다. 헤더 전용 부분만 쓴다(Asio, Beast). FetchContent로 Boost 전체를 받는 방식은 빌드 시간이 커서 쓰지 않는다.
- standalone Asio FetchContent는 이 전환 PR에서 삭제한다 (두 Asio를 함께 두지 않는다).

## 결과

- 이벤트 루프가 하나라 스레드 동기화가 필요 없다. SDD의 "단일 스레드 io_context" 구조를 그대로 유지한다.
- Boost 설치가 개발 환경 전제 조건이 된다. README와 클라우드 환경 설정 스크립트에 `libboost-dev` 설치를 추가한다.
- B보다 빌드가 느리고 실행 파일이 크다. 관제 GW에서는 문제가 되지 않는 수준으로 판단한다.
- B의 내장 backpressure 대신 대기열 상한을 직접 구현해야 한다. 대신 그 정책을 시험으로 고정할 수 있다.

## 면접 대비 한 줄

> "GW는 UDP와 타이머를 단일 스레드 Asio 루프 하나로 돌리고 있어서, WebSocket도 같은 루프에 붙는 Beast를 골랐습니다. uWebSockets는 더 가볍지만 자체 루프라 스레드 두 개 사이 동기화가 생기고, websocketpp는 2020년 이후 유지보수가 멈춰 최신 Asio에서 컴파일되지 않는 것을 시제품으로 확인했습니다."
