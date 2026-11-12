# 소프트웨어 시험 계획서 (STP)

| 항목 | 내용 |
|---|---|
| 문서번호 | UGVC-STP-001 |
| 시스템 | AI 표적 탐지 기반 UGV 원격 관제 시스템 (가칭 UGVC) |
| 버전 | v0.9 (초안) |
| 작성일 | 2026-11-10 |
| 관련 문서 | UGVC-SRS-001 v0.5, UGVC-ICD-001 v0.7, UGVC-SDD-001 v0.2.4, ADR-002 |

## 개정 이력

| 버전 | 날짜 | 변경 내용 |
|---|---|---|
| v0.1 | 2026-11-10 | 최초 작성. 통신(COM), 품질(QUAL-001) 시험 케이스 |
| v0.2 | 2026-11-10 | 검증 규칙 벡터(`validation.json`) 연결, TC-COM-002-11 케이스 보강(class_id, y·h 경계), TC-QUAL-001 체크리스트에 DETECTION·ACK·헤더 시험 반영 |
| v0.3 | 2026-11-11 | 추적성 정리(thermos 리뷰): TC-COM-005-08·007-02를 실제 GW 통합 시험으로 바꿈(이전 단위 시험은 범용 `Periodic`이거나 HEARTBEAT와 링크가 연결되지 않은 동어반복). 5장 출력 막힘 규칙에 stderr·EPIPE 반영. `check_stp_tests.py`가 시험 이름의 REQ ID가 SRS에 있는지도 검사 |
| v0.4 | 2026-11-11 | TC-COM-008 (IF-03 WebSocket 중계) 추가. PR #16 리뷰 반영: 008-06 기대 결과(느린 HMI 연결 종료), 008-07~10 추가. 2차 리뷰 반영: 008-09 동시 접속, 008-11·12 지연 상한. 3차 리뷰 반영: 008-12 EOF로 측정, 008-13·14 ping 응답 기한 경계. 4차 리뷰 반영: 008-12 기대 3.5~4.5s, 008-09 정확히 8개, 008-15 핸드셰이크 전 자원 상한 |
| v0.5 | 2026-11-12 | HMI 뼈대: TC-HMI-001-01~03, TC-HMI-002-01~03, TC-COM-008-16~20(HMI 쪽 IF-03 수신·재접속·신선도). 시험 수준에 vitest(U)와 시연 점검 D(`hmi/e2e/smoke.mjs`, Playwright) 추가. `check_stp_tests.py`가 vitest 이름도 검사 |
| v0.6 | 2026-11-12 | PR #19 리뷰 반영: HMI 쪽 IF-03 의무를 SRS v0.4의 REQ-HMI-006·007로 분리 → TC-COM-008-16~20을 TC-HMI-006·007로 옮김 (TC-COM-008은 GW 시험만). 시계 차, GPS_VALID, 무수신·접속 제한 시간, 궤적 구간, 표시 정책(view) TC 추가. 시연 점검은 화면 속성(data-level·data-stale)으로 판정하고 시계 차 ±로도 실행. vitest 시험은 describe 없이 it 제목 = 시험 이름, `check_stp_tests.py`는 `vitest list --json`으로 비교 |
| v0.7 | 2026-11-12 | PR #19 3차 리뷰 반영: TC-HMI-006-11~15(시계 계단 변화 회복, 기준선 재설정, stats 없는 연결, reconnect, 연결 이벤트), TC-HMI-001-07(GPS 깜박임), TC-HMI-002-06(GW→HMI 시각 변환), TC-HMI-007-05(폐기 계수). TC-HMI-007-03 범위 사례를 ICD 6장대로 확대, TC-HMI-006-07 수치 정정(2001ms), TC-HMI-001-03 진행 방향·궤적 점 수 판정, TC-HMI-001-06 연산 실행 결과 비교. `check_stp_tests.py`는 목록을 받지 못하면 안내와 함께 종료 코드 2 |
| v0.8 | 2026-11-12 | PR #19 4차 리뷰 반영: 기준선 재설정은 `resync()`(연결 끊김 아님)로 TC-HMI-006-15 기대값 변경, TC-HMI-006-16(전체 순환, `HmiController`), TC-HMI-006-11은 재접속 정확히 1회와 "GW 연결 끊김" 표시 없음까지 확인, TC-HMI-001-03은 실행한 그리기 연산으로 센 점 수(`data-drawn-points`), GW→HMI 시각 변환은 REQ-HMI-006 시험으로(TC-HMI-006-17), 불변성 확인은 TC-HMI-001-01에 합침, TC-HMI-007-05 입력은 필드를 다 갖춘 범위 밖 telemetry, TC-HMI-007-03에 `capture_ts` ≥ 2^53. `check_stp_tests.py`는 pytest 목록도 `--collect-only`로 받음. TC-HMI-002-06은 폐기(같은 시험을 TC-HMI-006-17로) |
| v0.9 | 2026-11-12 | PR #19 5차 리뷰 반영: TC-HMI-006-11은 페이지 안 `MutationObserver`로 배너 변화를 빠짐없이 기록해 "기준선 재설정 중" 있음·"GW 연결 끊김" 없음을 확인, TC-HMI-001-03은 Leaflet 레이어의 실제 점 수, TC-HMI-006-16에 재설정 시점 상한(6.25초)과 궤적 유지, TC-HMI-006-18(접속 이유 cause)·19(단조 시계 카운트다운)·TC-HMI-001-08(진짜 끊김 뒤 궤적 분리) 추가, TC-HMI-006-13 문구 정정, TC-HMI-007-03에 `capture_ts` 상한·`last_rx_ts` null인 CONNECTED. `check_stp_tests.py`는 TC 행의 해석하지 못한 항목과 형식이 틀린 REQ ID도 실패 |

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
| HMI (REQ-HMI-001·002 뼈대, 006, 007) | v0.5~0.6 | 경로점(REQ-HMI-001), 지연 시간(REQ-HMI-002)은 해당 기능 구현 시 |
| HMI-003~005, AI, LOG, PERF | | 해당 기능 구현 시 |

### 1.3 용어
용어는 루트 `GLOSSARY.md`를 따른다. 특히 **스트림**(송신원 + msg_type), **유효 패킷**, **유실**, **역순 도착**, **유실률**의 정의에 주의한다.

---

## 2. 시험 환경과 방법

### 2.1 시험 수준

| 기호 | 수준 | 도구 | 대상 |
|---|---|---|---|
| U | 단위 | GoogleTest (C++), pytest (Python), vitest (HMI TypeScript) | 직렬화, 검증 규칙, 링크 판정, seq·유실 처리, HMI 상태 갱신·재접속. 시간 판정은 모두 가짜 시계로 시험 |
| I | 통합 | pytest + GW 실행 파일 | 실제 주기·처리량이 필요한 항목 (REQ-COM-001 처리량, REQ-COM-007 주기) |
| D | 시연 점검 | `hmi/e2e/smoke.mjs` (Playwright chromium) + 실제 GW·SIM·HMI | 화면 표시 요구사항(SRS 검증 방법 D). 화면 요소를 자동으로 확인하고 화면을 캡처해 STR에 붙인다 |
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
- 테스트 이름: 요구사항 ID를 포함한다. 예: `TEST(LinkMonitor, REQ_COM_003_degraded_at_500ms)`, `def test_REQ_COM_001_10hz_no_loss()`, `it("REQ_HMI_001 track keeps ...")`
- vitest 시험은 3장 표에 "파일 이름 > 시험 이름"으로 적는다 (예: `hmiState.test.ts > REQ_HMI_002 starts with both links LOST like the GW`). HMI 시험 파일은 `describe`를 쓰지 않아 it 제목이 곧 시험 이름이다. `check_stp_tests.py`는 `vitest list --json`의 실제 목록과 비교한다
- 시험 이름에는 모두 REQ ID를 단다 (RTM grep에서 빠지지 않도록)
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
| TC-COM-008-09 | I | 8개 접속 후 9번째 접속. 16개 동시 접속 5회 | 9번째는 HTTP 503. 동시 접속에서도 열린 세션 ≤ 8(첫 회는 정확히 8), 나머지는 503 | `test_REQ_COM_008_session_limit`, `test_REQ_COM_008_session_limit_concurrent` |
| TC-COM-008-10 | I | 파일 디스크립터 상한 20(GW 기본 약 12개 + 여유 8, 핸드셰이크 전 연결 상한 16보다 작게)으로 GW 실행, TCP 연결 60개로 accept 실패 유발 | GW CPU 30% 미만, stderr에 "ws accept error" 기록, HEARTBEAT 간격 < 1.2s | `test_REQ_COM_008_accept_error_backs_off` |
| TC-COM-008-11 | I | 운용 조건: 기본 소켓 버퍼로 읽지 않는 HMI와, 아무것도 보내지 않고 받기만 하는 정상 HMI를 붙인 채 TELEMETRY 10Hz 6초 | 읽지 않는 HMI는 GW가 끊음(EOF), 정상 HMI는 열린 채 60개 모두 수신 | `test_REQ_COM_008_stalled_hmi_closed_within_lag_limit` |
| TC-COM-008-12 | I | pong을 보내지 않는 HMI(데이터는 읽어 버림)가 끊기는 시각 | 접속 후 3.5~4.5s (실측 4.00~4.02s: 2s 판정 틱에 ping, 다음 틱에 종료), stderr에 "ws close: lag limit" | `test_REQ_COM_008_lag_limit_timing` |
| TC-COM-008-13 | I | raw 소켓 HMI가 0.3s·1.6s 주기로 버퍼를 읽고 ping에 pong으로 답함, 20s | 끊기지 않음 (ping 응답 기한 2.0s 안) | `test_REQ_COM_008_hmi_answering_ping_within_limit_stays` |
| TC-COM-008-14 | I | 같은 HMI가 5s 주기로만 답함 | 20s 안에 끊기고 stderr에 "ws close: lag limit" | `test_REQ_COM_008_hmi_answering_ping_late_is_closed` |
| TC-COM-008-15 | I | 업그레이드 요청에 `Content-Length: 1000000` 본문을 붙인 연결 200개가 3초 동안 본문을 보냄, 이후 정상 HMI 접속 | GW RSS 증가 < 20MB, GW 계속 동작, 정상 HMI는 접속해 link_status를 받음 | `test_REQ_COM_008_pre_handshake_flood_bounded` |

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

### 3.10 REQ-HMI-001 지도 표시 (위치, 진행 방향, 궤적)

경로점 표시는 경로점 명령(12월 명령 작업, TBD-03)과 함께 TC를 추가한다. U는 `hmi/tests/`, D는 `hmi/e2e/smoke.mjs`(4장).

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-HMI-001-01 | U | 위치 유효 telemetry 2개 반영 | 현재 위치·방향은 마지막 값, 궤적은 한 구간에 두 점, 이전 상태들은 바뀌지 않음(지도 그리기가 참조 비교에 의존) | `hmiState.test.ts > REQ_HMI_001 telemetry updates position and appends to the track` |
| TC-HMI-001-02 | U | telemetry 3001개 반영 | 3000점까지 유지, 넘으면 가장 오래된 점부터 한 묶음(300점) 삭제, 최신 점은 남음 | `hmiState.test.ts > REQ_HMI_001 track never exceeds TRACK_MAX_POINTS and drops the oldest points in blocks` |
| TC-HMI-001-03 | D | SIM 10Hz 송신 중 화면 확인 | 차량 표시 1개, 화살표가 heading(87.25°)만큼 회전(`data-heading`, `rotate(87.25deg)`), Leaflet 궤적 레이어에 실제로 들어 있는 점 수(`data-drawn-points`)가 3s 동안 20개 이상 늘어남, 화면 캡처 | `hmi/e2e/smoke.mjs` 2단계 |
| TC-HMI-001-04 | U | 위치 유효 → GPS_VALID 없는 telemetry(lat=lon=0) | 위치·궤적은 마지막 유효 위치 그대로, 모드 등 다른 값은 최신 | `hmiState.test.ts > REQ_HMI_001 position without GPS_VALID does not move the vehicle or the track` |
| TC-HMI-001-05 | U | GPS 무효 10표본(약 1초), 차량 LOST, IF-03 연결 끊김 뒤의 유효 위치 | 각각 새 구간 시작 (끊긴 곳을 직선으로 잇지 않음) | `hmiState.test.ts > REQ_HMI_001 track starts a new segment after GPS loss, vehicle LOST or GW disconnect` |
| TC-HMI-001-06 | U | 연속된 apply() 결과로 그리기 연산, 무작위 8000메시지(GPS 무효 20%, 연결 끊김 1%)에서 연산을 실행한 결과, 6000점 동안 전체 다시 그리기 횟수, 조각 나누기 | extend·newLine·clear를 구분, 실행 결과를 끝점 공유로 이으면 상태 궤적과 같음, 조각 ≤200점, clear는 앞쪽 삭제 때만(301점마다) | `trackSync.test.ts > REQ_HMI_001 plans extend, new line and rebuild from successive apply() results`, `trackSync.test.ts > REQ_HMI_001 executing the planned ops always draws exactly the state track`, `trackSync.test.ts > REQ_HMI_001 rebuilds only when the oldest points are trimmed`, `trackSync.test.ts > REQ_HMI_001 chunks share end points so the drawn line has no gaps` |
| TC-HMI-001-08 | U | HmiController에서 IF-03 연결이 실제로 끊겼다가(close 1006) 재접속 | 궤적이 두 구간 (기준선 재설정과 달리 진짜 끊김은 궤적을 끊음) | `hmiController.test.ts > REQ_HMI_001 a real IF-03 disconnect starts a new track segment` |
| TC-HMI-001-07 | U | GPS_VALID가 한 표본씩 번갈아 40개 | 구간 하나에 유효 위치 20점 (점 하나짜리 구간이 생기지 않아 궤적이 그려짐) | `hmiState.test.ts > REQ_HMI_001 brief GPS_VALID flicker keeps the segment so the track stays drawable` |

### 3.11 REQ-HMI-002 상태 패널

지연 시간 표시는 영상-탐지 종단 지연(TBD-01) 측정과 함께 TC를 추가한다.

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-HMI-002-01 | U | 초기 상태 | 두 송신원 모두 LOST (GW 초기값과 같음), 차량 정보 없음 | `hmiState.test.ts > REQ_HMI_002 starts with both links LOST like the GW` |
| TC-HMI-002-02 | U | VEHICLE link_status(DEGRADED)와 stats 반영 | VEHICLE만 바뀌고 DETECTOR는 그대로, 유실률 반영 | `hmiState.test.ts > REQ_HMI_002 link_status and stats update per source` |
| TC-HMI-002-03 | D | GW 접속 → SIM 송신 → SIM 중지 | 차량 링크 LOST → CONNECTED(유실률 0.0%, 경과 0.x초) → 3s 안에 LOST, 모드·배터리 표시 | `hmi/e2e/smoke.mjs` 1~3단계 |
| TC-HMI-002-04 | U | HMI 시계가 GW보다 5s 늦을 때 링크 경과 시간 | 0.8s (음수가 아님), 수신 전이면 "수신 없음" | `view.test.ts > REQ_HMI_002 link age uses the connection baseline, not the raw GW clock` |
| TC-HMI-002-05 | U | 배터리 색, 위치 표시 | BATTERY_LOW 플래그가 있을 때만 경고색, GPS_VALID가 없으면 "위치 무효" | `view.test.ts > REQ_HMI_002 battery color follows BATTERY_LOW and position shows invalid without GPS_VALID` |

### 3.12 REQ-HMI-006 IF-03 연결 감시·재접속·데이터 신선도

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-HMI-006-01 | D | 실제 GW·HMI 연결 중 GW 종료, 같은 포트로 GW 재시작. 시계 차 0, +3s, −5s로 각각 실행 | 3s 안에 배너 bad("GW 연결 끊김 · N초 후 재접속")와 패널 흐림, 재시작 후 8s 안에 배너 ok(새 stats로 FRESH), 패널 흐림 해제. 시계 차가 있어도 1단계부터 ok | `hmi/e2e/smoke.mjs` 4·5단계 (`--clock-skew 3000`, `--clock-skew -5000`) |
| TC-HMI-006-02 | U | 가짜 소켓이 계속 접속 실패(close 1006) | 재접속 대기 1s, 2s, 4s, 5s, 5s | `gwConnection.test.ts > REQ_HMI_006 retries with 1s, 2s, 4s, 5s, 5s while the GW is unreachable` |
| TC-HMI-006-03 | U | 실패 2회 후 접속 성공, 다시 끊김 | 다음 대기가 1s로 돌아감 | `gwConnection.test.ts > REQ_HMI_006 resets the delay to 1s after a successful connection` |
| TC-HMI-006-04 | U | 열린 직후부터 4s 무수신, 메시지를 받을 때마다 다시 잼. 접속 시도가 5s 안에 열리지 않음 | 소켓을 닫고 재시도(사유 idle timeout / connect timeout), 버린 소켓의 이벤트는 전달 안 됨 | `gwConnection.test.ts > REQ_HMI_006 an open connection that receives nothing for 4s is closed and retried`, `gwConnection.test.ts > REQ_HMI_006 a connect attempt that does not open within 5s is closed and retried` |
| TC-HMI-006-05 | U | 소켓 생성이 예외(잘못된 주소)를 던짐 | 같은 재시도 경로, 간격은 계속 늘어남 | `gwConnection.test.ts > REQ_HMI_006 a socket constructor that throws goes through the same retry path` |
| TC-HMI-006-06 | U | stop() 뒤, start() 두 번 | 소켓 닫힘, 이후 메시지·close 전달 없음, 재시도 없음 / 살아 있는 소켓은 하나 | `gwConnection.test.ts > REQ_HMI_006 stop() closes the socket, stops all events and schedules no retry`, `gwConnection.test.ts > REQ_HMI_006 calling start() twice keeps a single live socket` |
| TC-HMI-006-07 | U | 신선도: 연결의 첫 stats 전(재접속 직후 포함), 마지막 stats 후 2000ms·2001ms, 기준선보다 2000ms·2001ms 늦게 도착 | WAITING, FRESH, STALE, FRESH, STALE | `hmiState.test.ts > REQ_HMI_006 WAITING before the first stats of a connection, for at most 2s`, `hmiState.test.ts > REQ_HMI_006 FRESH within 2s of the last stats, STALE after`, `hmiState.test.ts > REQ_HMI_006 STALE when stats arrive more than 2s later than the connection baseline` |
| TC-HMI-006-08 | U | GW·HMI 시계 차 −5s, +3s, +1h | 제때 온 stats는 FRESH, 2.5s 늦은 stats는 STALE (시계 차 무관) | `hmiState.test.ts > REQ_HMI_006 freshness ignores a constant clock difference between GW and HMI` |
| TC-HMI-006-09 | U | 배너와 흐림 정책 | 첫 접속 "GW 접속 중"(warn), 재접속 대기는 남은 초, 대기·지연·정상 구분. GW 끊김·지연이면 전체, 차량 링크가 CONNECTED가 아니면 차량 값만 흐림 | `view.test.ts > REQ_HMI_006 banner distinguishes first connect, disconnect with countdown, waiting, stale and ok`, `view.test.ts > REQ_HMI_006 dims everything when GW is down or stale, and vehicle values when the vehicle link is not CONNECTED` |
| TC-HMI-006-10 | U | 기본 주소 | 페이지 호스트를 따르고 VITE_GW_URL이 우선 | `gwConnection.test.ts > REQ_HMI_006 default URL follows the page host, override wins` |
| TC-HMI-006-11 | D | FRESH인 실제 연결에서 HMI 시계를 +3s 계단 변화. 페이지 안 `MutationObserver`가 배너 문구 변화를 모두 기록 | 15s 안에 GW WebSocket 생성이 정확히 1회 늘고 배너 ok. 기록에 "GW 데이터 지연 · 기준선 재설정 중"이 있고 "GW 연결 끊김"으로 시작하는 문구는 없음 | `hmi/e2e/smoke.mjs` 2b단계 |
| TC-HMI-006-12 | U | 연결 중 HMI 시계 +3s 계단 변화 후 1Hz stats, 재접속, 한 번 늦었다가 제때 온 stats | 늦음이 5s 이어지면 재설정 요청(그 전에는 아님), 재접속 뒤 FRESH, 제때 오면 늦음 해제 | `hmiState.test.ts > REQ_HMI_006 data that keeps arriving late asks for a resync, on-time data does not` |
| TC-HMI-006-13 | U | `ts`가 2^53−1인 메시지 하나로 기준선이 망가진 연결 | STALE이 되고 5s 뒤 재설정 요청 (재설정 뒤 회복 경로는 TC-HMI-006-16이 확인) | `hmiState.test.ts > REQ_HMI_006 one message with an absurd ts recovers through resync` |
| TC-HMI-006-14 | U | 연결 후 telemetry만 오고 stats가 오지 않음 | 2000ms까지 WAITING, 2001ms에 STALE (TC-HMI-006-07과 같은 시험) | `hmiState.test.ts > REQ_HMI_006 WAITING before the first stats of a connection, for at most 2s` |
| TC-HMI-006-15 | U | resync() 호출, 그 접속의 실패, 정지 뒤 호출. 배너 | 소켓을 닫고 기다리지 않고 새 소켓(CONNECTING cause resync), WAITING_RETRY 없음, 버린 소켓 이벤트 미전달, 그 접속이 실패하면 1s 재시도, 정지 뒤에는 아무것도 안 함. 재설정 중 배너는 warn "GW 데이터 지연 · 기준선 재설정 중" | `gwConnection.test.ts > REQ_HMI_006 resync() reconnects at once without counting a failure`, `view.test.ts > REQ_HMI_006 banner distinguishes first connect, disconnect with countdown, waiting, stale and ok` |
| TC-HMI-006-16 | U | 가짜 소켓·타이머·벽시계·단조 시계로 HmiController 구동(10Hz telemetry, 1Hz stats, 250ms tick, 화면은 `view()`): FRESH 연결에서 stats 직후 HMI 시계 +3s 계단 변화. 정상 연결 60s | 계단 뒤 5s 이상 6.25s 이하에 재접속 1회(CONNECTING resync), 배너 기록에 "기준선 재설정 중"이 있고 "GW 연결 끊김"이 없음, 새 연결 첫 stats로 FRESH, 궤적은 한 구간 그대로. 정상 연결에서는 재접속 0회 | `hmiController.test.ts > REQ_HMI_006 a clock step during a connection is recovered by resync within 6.25s, without a disconnect or a track break`, `hmiController.test.ts > REQ_HMI_006 on-time data never triggers a resync` |
| TC-HMI-006-17 | U | HMI 시계가 GW보다 약 5s 늦은 연결에서 GW 시각 변환, 연결 없음 | GW 시각 + 기준선, 연결이 없으면 null | `hmiState.test.ts > REQ_HMI_006 GW time converts to HMI time through the connection baseline` |
| TC-HMI-006-18 | U | 첫 접속, 실패 뒤 재접속, 기준선 재설정 | CONNECTING의 cause가 first, retry(failures 1), resync | `gwConnection.test.ts > REQ_HMI_006 connect attempts say why: first, retry with the failure count, resync` |
| TC-HMI-006-19 | U | 재접속 대기(1s) 중 벽시계 +3s, −60s, 단조 시계 0.6s·1.0s 진행 | 벽시계 변화와 무관하게 "1초 후 재접속", 단조 시계로만 줄어 "0초 후 재접속" | `hmiController.test.ts > REQ_HMI_006 retry countdown follows the monotonic clock, not wall clock changes` |

### 3.13 REQ-HMI-007 IF-03 수신 검증

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-HMI-007-01 | U | `hmi/tests/fixtures/gw_if03_sample.jsonl`(실제 GW 출력, `capture_gw_if03.py`로 생성) 파싱 | 4개 type 모두 받아들임 | `if03.test.ts > REQ_HMI_007 accepts every message type the real GW printed` |
| TC-HMI-007-02 | U | 정의 밖 필드가 붙은 telemetry | ICD 필드만 남김 | `if03.test.ts > REQ_HMI_007 keeps exactly the fields the ICD defines` |
| TC-HMI-007-03 | U | 깨진 JSON, 모르는 type, 빠진 필드, 정의 밖 열거값(mode, flags, state, source, class), ICD 6장 범위 밖 값(배터리 101·50.5, 위도 91, 방향 −1·360, 속도 30.01·1e308, seq·frame_id 2^32, img_w 0·4097, 객체 33개, w 0, x = img_w, x+w > img_w, y+h > img_h, conf 1.01, 유실률 1.5), errors 키 누락·추가·음수. `capture_ts` 1e300·2^70, `last_rx_ts`가 null인데 state CONNECTED. 경계 안 값(방향 359.99, 속도 30, seq 2^32−1, 상자가 이미지 끝에 닿음, 객체 32개, `capture_ts` 2^53+2·2^64, `last_rx_ts` null인 LOST) | 범위 밖은 모두 null, 경계 안은 받아들임 | `if03.test.ts > REQ_HMI_007 rejects malformed JSON, unknown types and missing fields`, `if03.test.ts > REQ_HMI_007 rejects values outside the ICD ranges and enums` |
| TC-HMI-007-04 | U | 텍스트가 아닌 프레임 | 텍스트만 전달 | `gwConnection.test.ts > REQ_HMI_007 passes text frames through and ignores binary` |
| TC-HMI-007-05 | U | 받은 텍스트: 깨진 JSON, 다른 필드는 다 갖추고 위도만 91인 telemetry, 정상 stats | 폐기 수 2, 정상 메시지는 반영되고 폐기 수는 그대로 (상태 패널 `data-testid="discarded"`에 표시) | `hmiState.test.ts > REQ_HMI_007 text that is not IF-03 is discarded and counted` |

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

### TC-HMI-001-03, TC-HMI-002-03, TC-HMI-006-01, TC-HMI-006-11 시연 점검

1. `gateway/build/ugvc_gateway`를 빌드하고 `hmi/`에서 `npm install`을 한다.
2. 저장소 루트에서 세 번 실행한다:

```bash
NODE_PATH=$(npm root -g) node hmi/e2e/smoke.mjs --screenshot hmi.png
```

```bash
NODE_PATH=$(npm root -g) node hmi/e2e/smoke.mjs --clock-skew 3000
```

```bash
NODE_PATH=$(npm root -g) node hmi/e2e/smoke.mjs --clock-skew -5000
```

3. 스크립트가 GW(빈 포트), vite, chromium을 띄우고 6단계(GW 접속 → SIM 송신 → 시계 +3초 계단 변화 → SIM 중지 → GW 중지 → GW 재시작)를 화면 속성(`data-level`, `data-stale`, `data-drawn-points`, `data-heading`), 정확한 문구, GW WebSocket 연 횟수로 확인한다. 페이지 오류가 하나라도 나면 실패한다.
4. 합격: 세 번 모두 마지막 줄이 `PASS`이고, 실행 뒤 `ugvc_gateway`·`vite`·`telemetry_sender` 프로세스가 남지 않는다. 화면 캡처는 STR(05)에 붙인다.

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
