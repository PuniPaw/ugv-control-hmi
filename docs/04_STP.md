# 소프트웨어 시험 계획서 (STP)

| 항목 | 내용 |
|---|---|
| 문서번호 | UGVC-STP-001 |
| 시스템 | AI 표적 탐지 기반 UGV 원격 관제 시스템 (가칭 UGVC) |
| 버전 | v0.4 (초안) |
| 작성일 | 2026-11-10 |
| 관련 문서 | UGVC-SRS-001, UGVC-ICD-001, ADR-002 |

## 개정 이력

| 버전 | 날짜 | 변경 내용 |
|---|---|---|
| v0.1 | 2026-11-10 | 최초 작성. 통신(COM), 품질(QUAL-001) 시험 케이스 |
| v0.2 | 2026-11-10 | 검증 규칙 벡터(`validation.json`) 연결, TC-COM-002-11 케이스 보강(class_id, y·h 경계), TC-QUAL-001 체크리스트에 DETECTION·ACK·헤더 시험 반영 |
| v0.3 | 2026-11-11 | 추적성 정리(thermos 리뷰): TC-COM-005-08·007-02를 실제 GW 통합 시험으로 바꿈(이전 단위 시험은 범용 `Periodic`이거나 HEARTBEAT와 링크가 연결되지 않은 동어반복). 5장 출력 막힘 규칙에 stderr·EPIPE 반영. `check_stp_tests.py`가 시험 이름의 REQ ID가 SRS에 있는지도 검사 |
| v0.4 | 2026-11-11 | TC-COM-008 (IF-03 WebSocket 중계) 추가. PR #16 리뷰 반영: 008-06 기대 결과(느린 HMI 연결 종료), 008-07~10 추가. 2차 리뷰 반영: 008-09 동시 접속, 008-11·12 지연 상한. 3차 리뷰 반영: 008-12 EOF로 측정·기대 2~4s, 008-13·14 ping 응답 기한 경계 |

---

## 1. 개요

### 1.1 목적
SRS의 각 요구사항을 어떤 시험으로, 어떤 기준으로 검증하는지 정의한다. 각 시험 케이스(TC)는 요구사항 ID와 자동화 테스트 이름으로 연결되어 RTM(06)과 STR(05)의 입력이 된다.

### 1.2 범위

| 영역 | v0.1 포함 | 추가 예정 |
|---|---|---|
| COM (REQ-COM-001~007) | ✅ | |
| QUAL (REQ-QUAL-001) | ✅ | REQ-QUAL-002 (고도화) |
| CMD, SAFE | | 12월 1–2주 (명령·Failsafe 구현 시) |
| HMI, AI, LOG, PERF | | 11/24 주 이후 해당 기능 구현 시 |

### 1.3 용어
용어는 루트 `GLOSSARY.md`를 따른다. 특히 **스트림**(송신원 + msg_type), **유효 패킷**, **유실**, **역순 도착**, **유실률**의 정의에 주의한다.

---

## 2. 시험 환경과 방법

### 2.1 시험 수준

| 기호 | 수준 | 도구 | 대상 |
|---|---|---|---|
| U | 단위 | GoogleTest (C++), pytest (Python) | 직렬화, 검증 규칙, 링크 판정, seq·유실 처리. 시간 판정은 모두 가짜 시계로 시험 |
| I | 통합 | pytest + GW 실행 파일 | 실제 주기·처리량이 필요한 항목 (REQ-COM-001 처리량, REQ-COM-007 주기) |
| R | 검사 | RTM 스크립트 출력 + 체크리스트 | REQ-QUAL-001 |

### 2.2 시계 주입
- 시간에 따라 동작이 바뀌는 GW 모듈(링크 판정, 유실률 창, HEARTBEAT 송신, 명령 재전송)은 `IClock`을 생성자로 받는다.
  - `now()`: `std::chrono::steady_clock::time_point`, 경과 시간 판정용 (시스템 시각이 바뀌어도 흔들리지 않음)
  - `epoch_ms()`: Unix epoch ms, 패킷 `timestamp`·`rx_ts`·기록용
- 단위 시험은 `FakeClock::advance(d)`로 시간을 진행한다. 실제 `sleep`을 쓰지 않는다.
- 링크 판정 주기는 SDD에서 50ms로 정하는 것을 가정한다. 통합 수준에서는 판정 지연 ≤ 판정 주기를 허용한다.

### 2.3 이벤트 확인
상태 전이 등 이벤트는 GW의 `IEventSink`로 내보낸다. 단위 시험은 `FakeEventSink`에 쌓인 이벤트를 확인한다. DB 저장은 REQ-LOG-001 TC에서 따로 검증한다.

### 2.4 공용 테스트 벡터
- 위치: `testdata/vectors/*.json`. 항목마다 `name`, 필드 값, 기대 바이트(`hex`), 기대 결과(수락 또는 오류 계수 이름)를 둔다.
- GoogleTest와 pytest가 **같은 파일**을 읽는다. Python 인코딩과 C++ 인코딩·디코딩이 바이트 단위로 일치함을 이것으로 보인다 (ADR-002).
- 이 문서의 TC는 입력을 벡터 이름으로 가리킨다 (예: `telemetry_nominal`).

### 2.5 통합 시험 확인 경로
- IF-03 WebSocket 구현 전(~11/23)까지 GW는 1Hz로 `stats` JSON 한 줄을 stdout에 출력한다. 구조는 ICD 7.1 `stats`의 `data`와 같다.
- pytest는 GW를 subprocess로 실행하고 stdout을 읽는다. IF-03 구현 후 확인 경로만 WebSocket으로 바꾼다.

### 2.6 결과 기록
- GoogleTest: `--gtest_output=xml`, pytest: `--junitxml` → JUnit XML
- STR(05)과 RTM(06)은 이 XML과 3장의 TC 표를 합쳐 스크립트로 생성한다.

### 2.7 이름 규칙
- TC ID: `TC-<영역>-<요구사항 번호>-<순번>` (예: `TC-COM-003-02`)
- 테스트 이름: 요구사항 ID를 포함한다. 예: `TEST(LinkMonitor, REQ_COM_003_degraded_at_500ms)`, `def test_REQ_COM_001_10hz_no_loss()`
- TC와 테스트는 1:1로 대응하며, 대응 관계는 3장 표의 "테스트" 열이 정본이다.

---

## 3. 시험 케이스

### 3.1 REQ-COM-001 TELEMETRY 파싱, 10Hz 무유실

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-COM-001-01 | U | 정상 TELEMETRY 디코딩 (`telemetry_nominal`) | 모든 필드가 벡터 값과 일치 | `Codec.REQ_COM_001_decode_telemetry` |
| TC-COM-001-02 | U | 필드 경계값 디코딩: lat ±900000000, lon ±1800000000, heading 35999, speed 3000, battery 0·100, mode 0·4 (`telemetry_bounds_*`) | 전부 수락, 값 일치 | `Codec.REQ_COM_001_decode_bounds` |
| TC-COM-001-03 | U | 인코딩 결과가 벡터 hex와 일치 (C++, Python 각각) | 바이트 단위 일치 | `Codec.REQ_COM_001_encode_matches_vector`, `test_REQ_COM_001_encode_matches_vector` |
| TC-COM-001-04 | U | status_flags 예약 비트(4–15)가 1인 패킷 | 수락, 예약 비트 무시 | `Validator.REQ_COM_001_reserved_bits_ignored` |
| TC-COM-001-05 | I | 10Hz TELEMETRY 600개(60초) 송신, 3회 반복 | 매 회 rx_count = 600, lost_count = 0, 오류 계수 전부 0 | `test_REQ_COM_001_10hz_no_loss` |

### 3.2 REQ-COM-002 검증 규칙 위반 폐기, 원인별 계수

공통 기대 결과: 해당 패킷 폐기, 표의 오류 계수만 1 증가, 나머지 계수와 수신 계수는 변화 없음.

입력은 `testdata/vectors/validation.json`의 case이며, 각 case의 `tc` 필드가 아래 TC ID다. 실패 case는 겨냥한 규칙 하나만 어기고 나머지는 맞게 만든다(예: sync만 틀린 프레임도 CRC는 다시 계산). 시험은 파일의 모든 case가 어느 TC에 속하는지도 확인한다(`Validator.AllVectorCasesAreCovered`).

| TC ID | 수준 | 내용 (ICD 9장 순서) | 오류 계수 | 테스트 |
|---|---|---|---|---|
| TC-COM-002-01 | U | 정상 패킷: TELEMETRY(nominal, 경계 최소·최대), ACK, DETECTION(nominal, count 0, count 32, 박스가 이미지 끝에 딱 맞음) | 없음 (수락) | `Validator.REQ_COM_002_accepts_valid` |
| TC-COM-002-02 | U | CRC-16/CCITT-FALSE 표준 벡터 ASCII `"123456789"` | 결과 `0x29B1` | `Crc16.REQ_COM_002_standard_vector` |
| TC-COM-002-03 | U | ① 수신 길이 0바이트, 19바이트, 1200바이트 초과(헤더·길이·CRC가 맞는 DETECTION count 200, 2237B와 1201B로 잘린 것) | length | `Validator.REQ_COM_002_rule1_too_short` |
| TC-COM-002-04 | U | ② sync `0x55AA` | sync | `Validator.REQ_COM_002_rule2_bad_sync` |
| TC-COM-002-05 | U | ③ version `0x02` | version | `Validator.REQ_COM_002_rule3_bad_version` |
| TC-COM-002-06 | U | ④ payload_len이 실제보다 1 큼 / 1 작음 | length | `Validator.REQ_COM_002_rule4_len_mismatch` |
| TC-COM-002-07 | U | ⑤ 페이로드 1비트 변조 / CRC 필드 변조 | crc | `Validator.REQ_COM_002_rule5_bad_crc` |
| TC-COM-002-08 | U | ⑥ 15001 포트에 DETECTION, 15002 포트에 TELEMETRY, 정의되지 않은 type `0x99` | type | `Validator.REQ_COM_002_rule6_type_not_allowed` |
| TC-COM-002-09 | U | ⑦ TELEMETRY N=15, ACK N=6, DETECTION count=2인데 N=17+11 | length | `Validator.REQ_COM_002_rule7_type_length` |
| TC-COM-002-10 | U | ⑧ TELEMETRY 범위 밖: lat 900000001, lon -1800000001, heading 36000, speed 3001, battery 101, mode 5 | range | `Validator.REQ_COM_002_rule8_telemetry_range` |
| TC-COM-002-11 | U | ⑧ DETECTION 범위 밖: img_w 0·4097, img_h 0, count 33 (길이는 count에 맞춤), confidence 10001, x = img_w, y = img_h, x + w > img_w, y + h > img_h, w 0, h 0, 표 6-3에 없는 class_id 3 | range | `Validator.REQ_COM_002_rule8_detection_range` |
| TC-COM-002-12 | U | ⑧ ACK result 3 | range | `Validator.REQ_COM_002_rule8_ack_range` |
| TC-COM-002-13 | U | 여러 규칙 동시 위반 (sync와 CRC, CRC와 범위) | 첫 실패 규칙만 증가 (sync, crc) | `Validator.REQ_COM_002_first_failure_only` |
| TC-COM-002-14 | U | 계수 누적: 같은 원인 3회, 다른 원인 1회 | 원인별로 정확히 누적 | `Validator.REQ_COM_002_counters_accumulate` |

규칙 ⑨(seq 순서)는 3.6절에서 다룬다.

### 3.3 REQ-COM-003 링크 상태 판정 (DEGRADED 0.5s, LOST 1.0s)

가짜 시계 사용. 시각 t는 마지막 유효 패킷 수신 후 경과 시간.

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-COM-003-01 | U | 첫 패킷 수신 전 | LOST | `LinkFixture.REQ_COM_003_initially_lost` |
| TC-COM-003-02 | U | t = 0.499s | CONNECTED | `LinkFixture.REQ_COM_003_connected_before_500ms` |
| TC-COM-003-03 | U | t = 0.500s | DEGRADED | `LinkFixture.REQ_COM_003_degraded_at_500ms` |
| TC-COM-003-04 | U | t = 0.999s | DEGRADED | `LinkFixture.REQ_COM_003_degraded_before_1s` |
| TC-COM-003-05 | U | t = 1.000s | LOST | `LinkFixture.REQ_COM_003_lost_at_1s` |
| TC-COM-003-06 | U | DEGRADED 중 유효 패킷 수신 | CONNECTED, t 재시작 | `LinkFixture.REQ_COM_003_valid_packet_resets` |
| TC-COM-003-07 | U | t = 0.4s에 폐기 패킷(CRC 오류) 수신 후 t = 0.5s | DEGRADED (폐기 패킷은 타이머를 갱신하지 않음) | `GatewayFixture.REQ_COM_003_rejected_does_not_reset` |
| TC-COM-003-08 | U | VEHICLE만 패킷 중단, DETECTOR는 계속 수신 | VEHICLE LOST, DETECTOR CONNECTED | `GatewayFixture.REQ_COM_003_sources_independent` |
| TC-COM-003-09 | U | 송신원의 아무 스트림(TELEMETRY 또는 ACK) 유효 패킷 | 둘 다 같은 송신원 타이머를 갱신 | `GatewayFixture.REQ_COM_003_any_stream_refreshes` |

### 3.4 REQ-COM-004 LOST 복귀, 상태 전이 기록

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-COM-004-01 | U | LOST 중 유효 패킷 수신 | 즉시 CONNECTED (DEGRADED를 거치지 않음) | `LinkFixture.REQ_COM_004_lost_to_connected` |
| TC-COM-004-02 | U | CONNECTED → DEGRADED → LOST → CONNECTED 진행 | 이벤트 3건, 각각 송신원·이전 상태·이후 상태·시각(epoch_ms) | `LinkFixture.REQ_COM_004_transition_events` |
| TC-COM-004-03 | U | 같은 상태 유지 중 (패킷 계속 수신, 또는 LOST 지속) | 이벤트 없음 | `LinkFixture.REQ_COM_004_no_event_without_change` |
| TC-COM-004-04 | U | 시작 시 첫 판정 | 이벤트 없음 | `LinkFixture.REQ_COM_004_no_event_at_startup` |

### 3.5 REQ-COM-005 유실 수, 최근 10초 유실률 1Hz 제공

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-COM-005-01 | U | seq 10, 11, 12 | 유실 0 | `SeqTracker.REQ_COM_005_contiguous_no_loss` |
| TC-COM-005-02 | U | seq 10, 13 | 유실 2 | `SeqTracker.REQ_COM_005_gap_counts_loss` |
| TC-COM-005-03 | U | u32 순환: 0xFFFFFFFF → 0 / 0xFFFFFFFE → 1 | 유실 0 / 2 | `SeqTracker.REQ_COM_005_wraparound` |
| TC-COM-005-04 | U | 10초 안에 수신 90, 유실 10 | 유실률 0.1 | `LossRate.REQ_COM_005_formula` |
| TC-COM-005-05 | U | 11초 전의 유실은 창에서 빠짐 | 해당 유실 제외 | `LossRate.REQ_COM_005_window_10s` |
| TC-COM-005-06 | U | 창 안에 수신·유실 모두 없음 | `null` | `LossRate.REQ_COM_005_empty_is_null` |
| TC-COM-005-07 | U | TELEMETRY 스트림 유실, ACK 스트림 연속 | ACK 스트림 유실 0 (스트림 독립) | `GatewayFixture.REQ_COM_005_streams_independent` |
| TC-COM-005-08 | I | 실제 GW를 실행해 VEHICLE stats 4개 수신 | 봉투 `ts` 간격 모두 1000 ± 100ms (보조 단위 시험: `Periodic` 범용 주기 판정, 멈춘 뒤 몰아서 내지 않음·주기 밀림 없음) | `test_REQ_COM_005_stats_1hz` |
| TC-COM-005-09 | U | LOST 후 첫 유효 패킷 seq가 크게 바뀜 (송신원 재시작) | 기준값으로 재설정, 유실·order 계수 변화 없음 | `GatewayFixture.REQ_COM_005_rebase_after_lost`, `SeqTracker.REQ_COM_005_rebase_accepts_any_seq` |

### 3.6 REQ-COM-006 역순 도착 폐기 (검증 규칙 ⑨)

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-COM-006-01 | U | seq 10 다음 9 | 폐기, order +1 | `SeqTracker.REQ_COM_006_older_rejected` |
| TC-COM-006-02 | U | seq 10 다음 10 (중복) | 폐기, order +1 | `SeqTracker.REQ_COM_006_duplicate_rejected` |
| TC-COM-006-03 | U | RFC 1982 경계: 차이 2^31 − 1 / 2^31 | 수락 / 폐기 | `SeqTracker.REQ_COM_006_rfc1982_boundary` |
| TC-COM-006-04 | U | seq 10, 12(유실 1), 11(늦게 도착) | 11 폐기, order +1, 유실은 1 유지 | `SeqTracker.REQ_COM_006_late_arrival_keeps_loss` |
| TC-COM-006-05 | U | 스트림의 첫 패킷 (seq 임의값) | 수락, 기준값으로 사용 | `SeqTracker.REQ_COM_006_first_packet_is_base` |

### 3.7 REQ-COM-007 HEARTBEAT 1Hz 송신

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-COM-007-01 | U | 가짜 시계로 2.95s 진행 (시작 즉시 1개, 이후 1s마다) | HEARTBEAT 3개: msg_type `0x20`, payload_len 0, seq 0·1·2, CRC 유효 | `Heartbeat.REQ_COM_007_emits_1hz` |
| TC-COM-007-02 | I | 실제 GW를 차량 패킷 없이 실행 (VEHICLE 링크 LOST) | HEARTBEAT 3개 수신, 간격 1000 ± 100ms, 그동안 link_status는 LOST | `test_REQ_COM_007_heartbeat_sent_while_vehicle_lost` |
| TC-COM-007-03 | I | 모의 SIM이 UDP 15010에서 30개 수신 | 간격이 모두 1000 ± 100ms | `test_REQ_COM_007_heartbeat_interval` |

### 3.8 REQ-COM-008 IF-03 WebSocket 중계

모두 실제 GW를 `--ws-port 0`으로 실행하고 Python `websockets` 클라이언트로 확인한다 (`tests/integration/test_ws_relay.py`).

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-COM-008-01 | I | `/ws`에 접속한 뒤 TELEMETRY 5개 송신 | WebSocket으로 받은 telemetry 5개가 stdout JSON Lines의 5개와 같다 | `test_REQ_COM_008_ws_relays_same_messages_as_stdout` |
| TC-COM-008-02 | I | TELEMETRY 1개로 VEHICLE을 CONNECTED로 만든 뒤 새로 접속 | 0.3s 안에 link_status(VEHICLE CONNECTED, DETECTOR LOST)와 송신원별 stats를 받는다 | `test_REQ_COM_008_new_session_gets_current_link_status` |
| TC-COM-008-03 | I | 클라이언트 2개 접속 후 TELEMETRY 3개 | 두 클라이언트가 같은 메시지를 같은 순서(seq 0·1·2)로 받는다 | `test_REQ_COM_008_all_sessions_receive` |
| TC-COM-008-04 | I | `/other`로 접속 | HTTP 404, 연결 거절 | `test_REQ_COM_008_other_path_rejected` |
| TC-COM-008-05 | I | IF-03 7.2 command 송신 | 세션이 유지되고 이후 stats를 계속 받는다 (명령 처리는 12월) | `test_REQ_COM_008_hmi_message_does_not_break_session` |
| TC-COM-008-06 | I | 핸드셰이크만 하고 읽지 않는 클라이언트가 있는 상태에서 DETECTION 6000개(그 클라이언트의 TCP 버퍼와 대기열 1000개를 넘김) 송신, 이후 새 클라이언트 접속. HEARTBEAT는 별도 스레드가 도착 시각으로 기록 | GW 계속 동작, HEARTBEAT 간격 < 1.2s, 새 클라이언트는 TELEMETRY 20개를 모두 받음, 멈춘 클라이언트는 GW가 연결을 끊음, stderr에 "ws close: queue full" 기록 | `test_REQ_COM_008_stalled_session_is_closed_without_blocking_others` |
| TC-COM-008-07 | I | HMI가 개행이 든 텍스트(`"x\nlink VEHICLE LOST -> CONNECTED"`) 송신 | stderr에 위조된 링크 전이 줄이 없고, 받은 텍스트는 JSON 문자열(`"x\\nlink ..."`)로 기록 | `test_REQ_COM_008_hmi_text_cannot_forge_diagnostics` |
| TC-COM-008-08 | I | `/ws?client=hmi`로 접속 | 접속되고 link_status를 받는다 (경로만 비교) | `test_REQ_COM_008_path_with_query_accepted` |
| TC-COM-008-09 | I | 8개 접속 후 9번째 접속. 16개 동시 접속 5회 | 9번째는 HTTP 503. 동시 접속에서도 열린 세션 ≤ 8, 나머지는 503 | `test_REQ_COM_008_session_limit`, `test_REQ_COM_008_session_limit_concurrent` |
| TC-COM-008-10 | I | 파일 디스크립터 상한 40으로 GW 실행, TCP 연결 60개로 accept 실패 유발 | GW CPU 30% 미만, stderr에 "ws accept error" 기록, HEARTBEAT 간격 < 1.2s | `test_REQ_COM_008_accept_error_backs_off` |
| TC-COM-008-11 | I | 운용 조건: 기본 소켓 버퍼로 읽지 않는 HMI와, 아무것도 보내지 않고 받기만 하는 정상 HMI를 붙인 채 TELEMETRY 10Hz 6초 | 읽지 않는 HMI는 GW가 끊음(EOF), 정상 HMI는 열린 채 60개 모두 수신 | `test_REQ_COM_008_stalled_hmi_closed_within_lag_limit` |
| TC-COM-008-12 | I | pong을 보내지 않는 HMI(데이터는 읽어 버림)가 끊기는 시각 | 접속 후 2.0~4.0s (판정 여유 0.5s), stderr에 "ws close: lag limit" | `test_REQ_COM_008_lag_limit_timing` |
| TC-COM-008-13 | I | raw 소켓 HMI가 0.3s·1.6s 주기로 버퍼를 읽고 ping에 pong으로 답함, 20s | 끊기지 않음 (ping 응답 기한 2.0s 안) | `test_REQ_COM_008_hmi_answering_ping_within_limit_stays` |
| TC-COM-008-14 | I | 같은 HMI가 5s 주기로만 답함 | 20s 안에 끊기고 stderr에 "ws close: lag limit" | `test_REQ_COM_008_hmi_answering_ping_late_is_closed` |

### 3.9 REQ-QUAL-001 직렬화·검증 규칙 단위 시험 (검사)

| TC ID | 수준 | 내용 | 합격 기준 | 테스트 |
|---|---|---|---|---|
| TC-QUAL-001-01 | R | RTM 스크립트 출력으로 체크리스트 확인: ICD 9장 규칙 ①~⑨, 6장 메시지 5종(인코딩·디코딩) | 각 항목에 대응하는 U 수준 TC가 1개 이상 있고 모두 통과 | (스크립트 `tools/rtm`) |

체크리스트:

| 항목 | 대응 TC |
|---|---|
| 규칙 ① ~ ⑧ | TC-COM-002-03 ~ 12 |
| 규칙 ⑨ | TC-COM-006-01 ~ 05 |
| CRC 알고리즘 | TC-COM-002-02 |
| TELEMETRY | TC-COM-001-01 ~ 04 |
| DETECTION, ACK | TC-COM-002-01, 09, 11, 12, `Codec.REQ_QUAL_001_detection_matches_vectors`, `Codec.REQ_QUAL_001_ack_matches_vectors` |
| 헤더·big-endian | `Frame.REQ_QUAL_001_*`, `Bytes.REQ_QUAL_001_*` |
| COMMAND, HEARTBEAT (GW 송신) | TC-COM-007-01 (COMMAND 인코딩은 CMD TC에서 추가) |

---

## 4. 통합 시험 상세 절차

### TC-COM-001-05 10Hz TELEMETRY 무유실

- **사전조건**: GW 빌드 완료, UDP 15001 비어 있음, 단일 PC(localhost)
- **절차**
  1. pytest가 GW를 subprocess로 실행하고 첫 `stats` 출력을 기다린다.
  2. Python 송신기가 `telemetry_nominal` 기반 TELEMETRY를 seq 0부터 100ms 간격으로 600개 보낸다.
  3. 마지막 송신 후 2초 동안 `stats` 출력을 읽는다.
  4. GW를 종료하고 1~3을 총 3회 반복한다.
- **합격 기준**: 매 회 VEHICLE의 `rx_count` = 600, `lost_count` = 0, `errors`의 모든 값 0
- **자동화**: `tests/integration/test_stp_com.py::test_REQ_COM_001_10hz_no_loss` (`-m slow`)

### TC-COM-007-03 HEARTBEAT 주기

- **사전조건**: GW 빌드 완료, UDP 15010에서 모의 SIM(pytest) 대기
- **절차**
  1. 모의 SIM 소켓을 연 뒤 GW를 실행한다.
  2. HEARTBEAT 31개를 수신하고 각 수신 시각(`time.monotonic()`)을 기록한다.
  3. 연속한 수신 시각 차이 30개를 계산한다.
- **합격 기준**: 30개 간격 모두 1000 ± 100ms, 모든 패킷이 검증 규칙 ①~⑤를 통과
- **자동화**: `tests/integration/test_stp_com.py::test_REQ_COM_007_heartbeat_interval` (`-m slow`)

---

## 5. 판정 규칙 보충 (SRS v0.2·ICD v0.2에 반영됨)

| 항목 | 규칙 | 관련 TC |
|---|---|---|
| 초기 링크 상태 | 첫 유효 패킷 수신 전은 LOST. 시작 시 초기값은 전이가 아니므로 이벤트 없음 | TC-COM-003-01, TC-COM-004-04 |
| 판정 경계 | 경과 시간 ≥ 0.5s DEGRADED, ≥ 1.0s LOST | TC-COM-003-02 ~ 05 |
| seq 판정 단위 | 스트림(송신원, msg_type)별. 링크 상태는 송신원별 | TC-COM-005-07, TC-COM-003-09 |
| 중복 seq | 역순 도착으로 보고 order 계수 | TC-COM-006-02 |
| 늦게 도착한 패킷 | order로 폐기, 이미 센 유실은 되돌리지 않음 | TC-COM-006-04 |
| RFC 1982 경계 | 차이가 정확히 2^31이면 "크지 않음" → 폐기 | TC-COM-006-03 |
| 최대 길이 | 수신 길이 > 1200이면 ①에서 length. 헤더·길이·CRC가 서로 맞아도 마찬가지 (GW 수신 버퍼는 1201B라 초과분은 잘려서 들어온다) | TC-COM-002-03 |
| DETECTION count 초과 | count와 길이가 맞으면 ⑦ 통과, ⑧ range로 폐기. 1200B 안에 들어가는 count 33~105가 해당 | TC-COM-002-11 |
| 출력이 막힐 때 | stdout·stderr를 읽는 쪽이 멈추거나 사라져도 판정 주기와 HEARTBEAT 1Hz는 계속된다. stdout 대기열(1000줄)을 넘거나 읽는 쪽이 닫혀 잃은 줄은 세어 stderr에 개수를 알린다. GW는 상속받은 fd의 플래그(O_NONBLOCK)를 바꾸지 않는다 | `test_REQ_COM_007_heartbeat_continues_when_stdout_reader_stalls`, `test_REQ_COM_007_heartbeat_continues_when_stderr_reader_stalls`, `LineSink.REQ_COM_007_publish_does_not_block_when_reader_stalls` |
| 유실률 | 유실 / (수신 + 유실), 1초 버킷 10개, 창이 비면 null. seq·유실은 스트림별로 세고, IF-03 `stats`는 송신원 단위이므로 그 송신원의 모든 스트림을 합산해 보고한다 | TC-COM-005-04 ~ 07 |
| HEARTBEAT | 차량 링크 상태와 무관하게 GW 동작 중 항상 1Hz 송신 | TC-COM-007-02 |
