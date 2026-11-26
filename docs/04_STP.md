# 소프트웨어 시험 계획서 (STP)

| 항목 | 내용 |
|---|---|
| 문서번호 | UGVC-STP-001 |
| 시스템 | AI 표적 탐지 기반 UGV 원격 관제 시스템 (가칭 UGVC) |
| 버전 | v0.30 (초안) |
| 작성일 | 2026-11-10 |
| 관련 문서 | UGVC-SRS-001 v0.11, UGVC-ICD-001 v0.15, UGVC-SDD-001 v0.7.2, ADR-002 |

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
| v0.10 | 2026-11-12 | PR #19 6차 리뷰 반영: TC-HMI-006-16을 ms 단위·계단 위상 4가지·ICD 값 기준으로(첫 늦은 stats부터 5~5.25초, 계단부터 ≤6.25초), TC-HMI-001-09(느린 재설정 접속은 궤적 분리), TC-HMI-006-20(재설정 시계는 stats에서만 시작), TC-HMI-002-07(상태 패널 행이 같은 판정에서 나옴) 추가. TC-HMI-001-03은 개발 서버 훅으로 레이어 점 수. TC-HMI-006-01 기대 문구에 "재접속 중" 포함. 표 순서 정리, 후속 범위(경로점·지연 시간) 명시. `check_stp_tests.py`는 표 머리의 "테스트" 열로 해석하고 REQ 형식 검사의 오탐·누락을 고침(자체 시험 `tools/tests/`) |
| v0.11 | 2026-11-12 | PR #19 7차 리뷰 반영: TC-HMI-001-09를 seq 간격 기준(느린 접속·버린 적체 두 경우)으로 바꾸고 TC-HMI-001-10(간격 경계 9·10, u32 순환) 추가, TC-HMI-006-16에 stats→tick 위상 4가지와 위상별 정확한 재설정 시각, TC-HMI-002-07에 폐기 수. 2.7에 도구 자체 시험 예외. `check_stp_tests.py`는 TC 행의 모든 칸에서 시험 이름을 모으고, "테스트" 열이 없는 표의 TC 행을 오류로, 대문자 REQ 후보는 REQUEST 등을 제외 |
| v0.12 | 2026-11-12 | PR #19 8차 리뷰 반영: TC-HMI-001-09를 두 시험으로 나누고 (b)의 주장 범위를 실제 입력으로 좁힘, TC-HMI-001-10에 seq 역행·같은 seq, TC-HMI-006-16 기대 결과를 ICD 범위(5000 이상 5250 미만)로, 폐기 수 표시 확인을 REQ-HMI-007 시험으로(TC-HMI-007-05). 2.7에 REQ 모양 약어 금지·도구 커밋 규칙. `check_stp_tests.py`는 표 밖 줄·앞 공백 표 행도 수집, TC 행은 머리 행으로 보지 않음, 옵션만 있는 TC 행 오류, REQ 후보를 ID 모양으로 좁힘 |
| v0.13 | 2026-11-14 | 명령 중계 구현: TC-CMD-002-01~09(GW 재전송·IF-03 command·cmd_result), TC-CMD-003-01~05(SIM 중복 cmd_id), TC-CMD-005-01~03(명령 이벤트 기록), TC-CMD-006-01~04(SIM REJECTED_INVALID, xfail 해제). TC-COM-008-05·07 기대 결과를 명령 처리 구현에 맞춤(거절 사유와 함께 기록) |
| v0.14 | 2026-11-14 | PR #24 1차 리뷰 반영: TC-CMD-002-05를 req_id 규칙(대기 중·최근 결과·형식 오류)으로 바꾸고 TC-CMD-002-10~13(재전송 예정 시각, ACK 스트림 ⑨ 제외, SIM 수신부 재시작, 같은 req_id 다른 HMI·결과 전체 전달·형식 오류), TC-CMD-003-06·07(GW 재시작 cmd_id, 결과를 놓친 HMI 재송신) 추가 |
| v0.15 | 2026-11-14 | PR #24 2차 리뷰 반영: TC-COM-005-07을 ACK 스트림 유실 제외로 바꿈(ACK가 유실 계산에서 빠져 기존 "스트림 독립" 시험이 아무것도 증명하지 못하게 됨), TC-COM-006-06(ACK 스트림 ⑨ 제외) 추가, TC-CMD-002-11은 같은 시험을 가리킴. TC-CMD-003-07을 TC-CMD-002-14로 옮김(req_id 규칙은 GW 동작이라 SRS v0.7 REQ-CMD-002). TC-CMD-002-15~17(거절한 req_id 기억, 판정이 1s 넘게 늦은 재전송, 고친 명령을 거절한 req_id로) 추가 |
| v0.16 | 2026-11-14 | PR #24 3차 리뷰 반영: TC-CMD-002-11 삭제(TC-COM-006-06과 같은 시험이라 REQ-CMD-002 추적에 REQ-COM-006 시험이 끼었음), TC-CMD-002-16 기대 시각을 새 규칙(실제 송신 + 0.95s)으로, TC-CMD-002-18(0.9s 늦은 판정)·19(거절 기록과 송신 기록의 상한 분리) 추가. 관련 문서 버전 갱신 |
| v0.17 | 2026-11-20 | 2.7에 REQ ID가 여러 개인 시험 이름 규칙(모두 추적 대상, 모두 SRS에 있어야 함). `check_stp_tests.py`는 이름의 REQ ID를 전부 SRS와 대조(이전에는 첫 ID만, 이슈 #22) |
| v0.18 | 2026-11-20 | PR #34 1차 리뷰 반영: 2.7을 "시험 이름에 REQ ID는 하나만"으로 바꿈(v0.17의 "ID마다 추적 대상"은 TC와 시험 1:1 규칙, v0.16의 TC-CMD-002-11 삭제 결정과 어긋났음). RTM 생성 스크립트 문장은 앞으로 만들 스크립트에 대한 요구로 고침. `check_stp_tests.py`는 서로 다른 REQ ID가 둘 이상인 시험 이름을 실패로, 앞이 대문자인 REQ(PREREQ 등)는 ID로 보지 않음 |
| v0.19 | 2026-11-20 | Failsafe 구현: TC-SAFE-001-01~05(HEARTBEAT 3.0s 두절 정지), TC-SAFE-002-01~07(복구 뒤 재개 금지, 해제 명령, 두절 중 REJECTED_STATE, GW 두절 통합). 2.2에 SIM 시각 주입. SIM이 한 프로세스가 되어 TC-CMD-003-04·006-04의 실제 UDP 시험은 `python -m ugvc_sim` 본체(`app.run`)를 구동 |
| v0.20 | 2026-11-20 | PR #36 1차 리뷰 반영: TC-SAFE-001-07(GW 송신 실패에도 SIM이 죽지 않고 Failsafe), TC-SAFE-002-08(해제 경계: 복구 HEARTBEAT 뒤 +2.999s 수락, +3.000s REJECTED_STATE) 추가. TC-SAFE-001-04는 전이 1회, TC-SAFE-002-05는 새 명령 뒤 상태, TC-SAFE-002-06은 복구 구간 전체를 판정. TC-CMD-002-12를 SIM 한 프로세스 구조에 맞춤(차량 링크는 시험의 텔레메트리 송신기로 CONNECTED 유지, 재시작 내내 LOST 없음). 2.7에 SIM 운동 모사 시험 예외, 3.14·4장의 옛 구성요소 이름 정정 |
| v0.21 | 2026-11-23 | PR #36 2차 리뷰 반영: TC-SAFE-001-07이 Failsafe 시점(TELEMETRY seq 29는 플래그 없음, seq 30 = 3.0s부터 FAILSAFE_ACTIVE)과 ACK 송신 실패 경로까지 판정(3.9s 변이에서 실패함을 확인). TC-SAFE-001-08(루프가 멈췄다 재개: 몰아 보내지 않고 첫 TELEMETRY가 Failsafe), TC-SAFE-002-09(복구 뒤 RTB는 `Simulator`에 준 출발 지점으로) 추가. TC-CMD-002-12 기대 결과를 실제 판정(재시작 뒤 보고 2회)대로. v0.19 이력에서 빠진 TC-SAFE-001-06(v0.19에 이미 있던 행)을 여기 적어 보완(v0.20에서 v0.19 행을 직접 고쳤던 것을 되돌림). PR #34(v0.17·v0.18)가 먼저 병합되어 이 PR의 v0.17~v0.19를 v0.19~v0.21로 다시 매김 |
| v0.22 | 2026-11-20 | HMI 명령 패널: TC-CMD-001-01~10(운용자 확인 절차, command 텍스트·송신, cmd_result 표시, 연결이 끝난 뒤 자동 재송신 금지와 재시도 새 req_id(이슈 #27), 경로점 입력), TC-CMD-004-01·02(차량 링크 LOST·GW 연결 없음이면 비활성과 사유), TC-HMI-007-06(cmd_result 수신 검증) 추가. 시연 점검에 명령 단계(2c·2d)와 3·4단계의 명령 차단 확인 추가 |
| v0.23 | 2026-11-20 | PR #35 1차 리뷰 반영: TC-CMD-004-03(재접속·기준선 재설정 뒤 이 연결의 차량 link_status 전에는 차단) 추가, TC-CMD-004-01 기대 결과(첫 link_status 전 사유 "차량 링크 상태 수신 전", IF-03 끊김이면 확인 단계 닫힘). TC-CMD-001-11(기준선 재설정은 결과 대기 유지, 결과 기한 5s 뒤에만 결과 미확인) 추가, TC-CMD-001-05는 진짜 끊김만. TC-CMD-001-03(결과 대기 중 명령은 기록 상한으로 밀리지 않음), 001-06(다시 보내기 경고, 이미 다시 보낸 명령·결과 대기 중 명령은 다시 보내기 없음), 001-07(소수 8자리 거절), 001-09(경고 확인, 이전 행에 다시 보내기 없음, `cmd sent`는 req_id별 하나로 비교), TC-HMI-007-06(TIMEOUT은 attempts 4만) |
| v0.24 | 2026-11-20 | PR #35 2차 리뷰 반영: 행별 다시 보내기와 결과 기한을 없앰. TC-CMD-001-06을 "다시 시도는 경고가 붙은 새 명령(새 req_id)"으로, TC-CMD-001-11을 "결과가 얼마나 늦어도(30s), 재설정을 넘어도 결과 대기"로 바꿈(결과 기한 경계 시험 삭제), TC-CMD-001-12(경고한 명령의 늦은 결과가 열린 확인 단계를 닫아 송신 0) 추가. TC-CMD-001-03은 결과 미확인도 상한으로 밀리지 않음, 001-05는 끊긴 뒤 새 연결의 결과 반영, 001-09는 smoke 2d를 새 흐름으로 |
| v0.25 | 2026-11-23 | PR #35 3차 리뷰 반영: 기준선 재설정도 결과 대기 명령을 결과 미확인으로. TC-CMD-001-11(b) 기대 결과를 "재설정 뒤 결과 미확인, 새 연결 뒤 확정된 결과는 반영"으로 바꾸고 시험 이름 변경, TC-CMD-001-13(버린 적체에서 잃은 결과는 결과 미확인, 더 새 같은 종류의 결과로 경고 끝) 추가, TC-CMD-001-06에 같은 종류의 가장 최근 명령만 경고. 없어진 다시 보내기 문구 정리(TC-CMD-001-04 기대 결과, 4장 시연 절차) |
| v0.26 | 2026-11-23 | PR #35 4차 리뷰 반영과 `main`(PR #36) merge: TC-CMD-001-13에 "재설정이 열린 확인 단계를 닫아 송신 0" 시험 추가, TC-CMD-001-06에 "같은 종류에 결과 대기 명령이 있으면 더 새 명령이 결과를 받아도 경고". PR #36이 SIM을 한 프로세스로 합쳐 시연 점검 2c는 SIM 자체의 ACK를, 2d는 SIM을 `--drop-first`로 다시 띄워 ACK 없는 상태를 씀(TC-CMD-001-08·09). 이 PR의 이력 행을 v0.22~v0.25, 절을 3.20·3.21로 다시 매김 |
| v0.27 | 2026-11-25 | HMI 경고 체계(SRS v0.9 표 3-1·3-2): 3.22 TC-HMI-003-01~08(수명: 해제됨·확인됨·재발·ADVISORY·명령 결과 경고, 등급별 순서, 시연 점검), 3.23 TC-HMI-004-01~07(조건별 등급, 받지 않은 값·첫 접속은 경고 아님, GW 끊김 중 마지막 값, LOST 경고는 그 link_status로 낸 화면에, 데이터 지연은 tick 하나 안, 명령 결과 경고, 시연 점검). REQ-HMI-004 2.0s 측정 구간(GW의 마지막 유효 패킷 수신 시각 `last_rx_ts` → LOST 경고가 DOM에 ACTIVE로 들어간 시각)과 방법을 3.23에 정함. 시연 점검 1·2·2b·2d·3·4·5단계에 경고 확인 추가. 링크 상태를 받기 전은 LOST가 아니라 상태 없음("—")으로 바꿔 TC-HMI-002-01 시험 이름·기대 결과 변경 |
| v0.28 | 2026-11-25 | DB 로그 저장: TC-LOG-001-01~08(행 변환, 실제 PostgreSQL 저장 내용·이벤트), TC-LOG-003-01~12(재접속 간격, `DbSink` 비차단·원인별 버림·재접속·종료, 저장 끄기, 없는 DB, DB 재시작, DB 서버 정지). 2.3 이벤트 확인을 실제 구조(반환값 `take_transitions`·`take_events`, DB 저장은 TC-LOG)로 고침(`IEventSink`·`FakeEventSink`는 만들지 않았다). 2.8 PostgreSQL 통합 시험 환경과 skip 규칙. 3.16 머리말(PostgreSQL 저장은 TC-LOG-001-08). 1.2 범위에 LOG |
| v0.29 | 2026-11-25 | PR #50 1차 리뷰 반영: 2.2에 실제 스레드 시험 예외(`LineSink`·`DbSink`, 시간 상한이 아니라 완료·계수로 판정). TC-LOG-003-04 삭제(순서대로 저장은 REQ-LOG-001의 증거라 TC-LOG-001-09로 옮김). TC-LOG-003-05·08은 시간 상한 대신 완료로 판정, TC-LOG-003-07을 "저장 중 연결 유실: 그 행 write_error, 나머지 db_down, 바로 재접속"으로 바꿈(쓰기 실패를 연결 오류와 데이터 오류로 나눔, SDD v0.7.1). TC-LOG-003-13(접속 실패 사유가 바뀌면 다시 알림), 14(거절 행만 rejected, 연결·다른 행 유지), 15(EUC_KR DB 통합) 추가. TC-LOG-001-07은 폐기 프레임 뒤 유효 프레임이 저장된 다음 행을 다시 셈, TC-LOG-003-11은 재접속 뒤 보낸 기록으로 판정. 버림 원인에 rejected |
| v0.30 | 2026-11-26 | PR #50 2차 리뷰 반영(SRS v0.11 REQ-LOG-003, SDD v0.7.2: 저장 실패는 종류를 가리지 않고 그 행만 write_error, 대기열 유지, 다시 접속). TC-LOG-003-07을 "저장 실패 한 번: 그 행만, 대기열 유지, 바로 재접속(테이블 재생성)"으로, TC-LOG-003-14를 "연결 직후 실패는 재접속 간격, 대기열 유지"로 바꿈(v0.29의 행 거절 rejected 판정 삭제). TC-LOG-003-08에 저장 중 행 계수와 close 두 번, TC-LOG-003-15 기대값을 write_error로. TC-LOG-003-16(테이블 삭제 뒤 회복, 통합), 17(DB 스레드 예외) 추가. 버림 0 판정은 종료 때 최종 누계 줄로. 2.2 예외를 `DbSink`로 좁힘(`LineSink` 시험은 시간 상한을 씀) |

---

## 1. 개요

### 1.1 목적
SRS의 각 요구사항을 어떤 시험으로, 어떤 기준으로 검증하는지 정의한다. 각 시험 케이스(TC)는 요구사항 ID와 자동화 테스트 이름으로 연결되어 RTM(06)과 STR(05)의 입력이 된다.

### 1.2 범위

| 영역 | v0.1 포함 | 추가 예정 |
|---|---|---|
| COM (REQ-COM-001~007) | ✅ | |
| QUAL (REQ-QUAL-001) | ✅ | REQ-QUAL-002 (고도화) |
| CMD (REQ-CMD-001~006) | v0.13, 001·004는 v0.22 | |
| SAFE (REQ-SAFE-001·002) | v0.19 | |
| HMI (REQ-HMI-001·002 뼈대, 006, 007) | v0.5~0.6 | 경로점(REQ-HMI-001), 지연 시간(REQ-HMI-002)은 해당 기능 구현 시 |
| LOG (REQ-LOG-001·003) | v0.28 | REQ-LOG-002 사후 재생 (고도화) |
| HMI-003~005, AI, PERF | | 해당 기능 구현 시 |

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
- SIM의 `Simulator`(SDD 6.2)는 시계 객체 대신 모든 메서드가 시각 `now`(단조 시계 초)를 인자로 받는다. 단위 시험은 시각 값을 직접 넣어 3.0s 경계를 시험한다.
- 링크 판정 주기는 SDD에서 50ms로 정하는 것을 가정한다. 통합 수준에서는 판정 지연 ≤ 판정 주기를 허용한다.
- 예외: `DbSink`(전용 DB 스레드)의 단위 시험은 스레드 사이 동작이 대상이라 실제 스레드를 쓰고, 결과가 보일 때까지 짧게 반복 확인(sleep)한다. 판정은 시간 상한이 아니라 완료와 계수로 한다. 예: "막히지 않음"은 가짜 DB가 멈춘 채로 publish 1만 번이 끝나는 것, "바로 재접속"은 재접속 간격을 1시간으로 둬도 다시 접속하는 것, "간격 적용"은 알림의 다음 시도까지 시간(`retry_in`). 재접속 간격 자체는 `ReconnectSchedule`을 가짜 시계로 시험한다(TC-LOG-003-01~03). `LineSink`(전용 쓰기 스레드) 시험도 실제 스레드를 쓰지만 "막히지 않음"을 시간 상한(500ms)으로 본다.

### 2.3 이벤트 확인
GW의 이벤트는 콜백이 아니라 반환값이다. 링크 전이는 `Gateway::take_transitions()`, 명령 사건은 `CommandTracker::take_events()`로 꺼낸다(SDD 4장·6.1). 단위 시험은 꺼낸 값을 확인한다. 이 값을 DB 행으로 바꾸는 것(`dblog::link_row`·`command_row`)은 TC-LOG-001 단위 시험이, 실제 DB 저장은 TC-LOG-001 통합 시험이 따로 검증한다.

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
- 3장 TC 표의 "테스트" 열(머리 행의 열 이름으로 찾는다)에 적은 백틱 항목은 모두 시험 이름(gtest·pytest·vitest) 또는 시연 점검 참조여야 한다. 해석하지 못한 항목이 있으면 `check_stp_tests.py`가 실패한다
- vitest 시험은 3장 표에 "파일 이름 > 시험 이름"으로 적는다 (예: `hmiState.test.ts > REQ_HMI_002 starts with no link state until the GW reports one`). HMI 시험 파일은 `describe`를 쓰지 않아 it 제목이 곧 시험 이름이다. `check_stp_tests.py`는 `vitest list --json`의 실제 목록과 비교한다
- 시험 이름에는 모두 REQ ID를 단다 (RTM grep에서 빠지지 않도록). 예외: 검사 도구 자체 시험(`tools/tests/`)은 제품 요구사항의 증거가 아니므로 REQ ID를 달지 않고 STP·RTM 대상이 아니다. 도구만 바꾸는 커밋도 같은 이유로 REQ ID 없이 `test(tools):`·`fix(tools):`로 쓴다
- 예외: SIM 차량 운동 모사 자체의 정확도 시험(`sim/tests/test_vehicle.py`: 직진, 경로점·복귀 주행, 도착 뒤 STANDBY, 배터리)은 시험 장비(차량 모사기)의 동작이라 REQ ID를 달지 않고 STP·RTM 대상이 아니다. 명령·Failsafe가 차량 상태에 미치는 영향 중 요구사항에 해당하는 것(정지 유지, 재개 금지·해제)은 `Simulator` 시험(TC-SAFE)으로 추적한다
- 시험 하나는 요구사항 하나의 증거다. 시험 이름에는 REQ ID를 하나만 단다(같은 ID를 되풀이한 것은 하나로 본다). 서로 다른 ID가 둘 이상이면 RTM grep이 한 시험을 두 요구사항에 이어 아래의 TC와 시험 1:1 규칙이 깨지므로, `check_stp_tests.py`가 실패한다. 이름의 ID는 모두 SRS와 대조한다. 다른 요구사항도 증명해야 하면 그 요구사항의 TC와 시험을 따로 둔다. 앞으로 만들 RTM 생성 스크립트도 시험마다 이름의 ID 하나로 요구사항을 이어야 한다
- 시험 이름에서 `req`·`REQ` 뒤에 영역 이름과 숫자가 바로 오는 약어(예: `req_cmd_3`)는 REQ ID와 구분할 수 없어 형식 오류로 걸린다. 이런 약어는 쓰지 않는다(`request_cmd_3`처럼 풀어 쓴다)
- TC와 테스트는 1:1로 대응하며, 대응 관계는 3장 표의 "테스트" 열이 정본이다.


### 2.8 PostgreSQL 통합 시험 환경
- REQ-LOG 통합 시험(`tests/integration/test_db_log.py`)은 시험 파일마다 한 번 임시 디렉터리에 `initdb`로 클러스터를 만들고 유닉스 소켓으로만 띄운다(`tests/integration/pg_cluster.py`, TCP 포트를 열지 않음). 시험마다 빈 DB를 만들고 GW가 접속하며 스키마를 만든다. 저장 내용은 `psql`로 읽는다.
- root로 실행하면 `postgres` 사용자로 서버를 띄운다(`initdb`는 root로 실행할 수 없다). 서버 프로그램 위치는 `/usr/lib/postgresql/*/bin`, 다르면 `UGVC_PG_BIN`으로 준다.
- PostgreSQL 서버·`psql`·`postgres` 사용자(root일 때)가 없으면 PostgreSQL이 필요한 시험만 skip하고, 사유를 pytest 요약 끝의 "환경 때문에 skip한 시험" 절에 출력한다. TC-LOG-003-09·10은 PostgreSQL 없이도 돈다.
- 필요한 패키지(Ubuntu): 빌드 `libpq-dev`, 시험 `postgresql`(서버), `postgresql-client`.

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
| TC-COM-005-07 | U | TELEMETRY seq 0, ACK seq 0·10, TELEMETRY seq 5 | 유실 4(TELEMETRY만), ACK seq 건너뜀은 유실 아님, order 0 (ICD 9장 ACK 제외) | `GatewayFixture.REQ_COM_005_ack_stream_is_not_counted_for_loss` |
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
| TC-COM-006-06 | U | 텔레메트리로 CONNECTED인 차량 링크에서 ACK seq 41, 42 다음 0, 0 | ACK는 모두 받음(ACK 스트림은 ⑨ 제외), order 0, 텔레메트리 역순은 그대로 폐기 | `GatewayFixture.REQ_COM_006_ack_stream_is_not_order_checked` |

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
| TC-COM-008-05 | I | IF-03 7.2 command 송신 | 세션이 유지되고 이후 stats를 계속 받는다 (명령 처리 결과는 TC-CMD-002) | `test_REQ_COM_008_hmi_message_does_not_break_session` |
| TC-COM-008-06 | I | 핸드셰이크만 하고 읽지 않는 클라이언트가 있는 상태에서 DETECTION 6000개(그 클라이언트의 TCP 버퍼와 대기열 1000개를 넘김) 송신, 이후 새 클라이언트 접속. HEARTBEAT는 별도 스레드가 도착 시각으로 기록 | GW 계속 동작, HEARTBEAT 간격 < 1.2s, 새 클라이언트는 TELEMETRY 20개를 모두 받음, 멈춘 클라이언트는 GW가 연결을 끊음, stderr에 "ws close: queue full" 기록 | `test_REQ_COM_008_stalled_session_is_closed_without_blocking_others` |
| TC-COM-008-07 | I | HMI가 개행이 든 텍스트(`"x\nlink VEHICLE LOST -> CONNECTED"`) 송신 | stderr에 위조된 링크 전이 줄이 없고, 받은 텍스트는 거절 사유와 함께 JSON 문자열(`"x\\nlink ..."`)로 기록 | `test_REQ_COM_008_hmi_text_cannot_forge_diagnostics` |
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
| 규칙 ⑨ | TC-COM-006-01 ~ 06 |
| CRC 알고리즘 | TC-COM-002-02 |
| TELEMETRY | TC-COM-001-01 ~ 04 |
| DETECTION, ACK | TC-COM-002-01, 09, 11, 12, `Codec.REQ_QUAL_001_detection_matches_vectors`, `Codec.REQ_QUAL_001_ack_matches_vectors` |
| 헤더·big-endian | `Frame.REQ_QUAL_001_*`, `Bytes.REQ_QUAL_001_*` |
| COMMAND, HEARTBEAT (GW 송신) | TC-COM-007-01 (COMMAND 인코딩은 CMD TC에서 추가) |

### 3.10 REQ-HMI-001 지도 표시 (위치, 진행 방향, 궤적)

**후속 범위:** REQ-HMI-001의 "경로점" 표시는 아직 TC가 없다. 경로점 명령(12월 명령 작업, TBD-03)과 함께 추가한다. RTM에서 이 부분은 미검증으로 보인다. U는 `hmi/tests/`, D는 `hmi/e2e/smoke.mjs`(4장).

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-HMI-001-01 | U | 위치 유효 telemetry 2개 반영 | 현재 위치·방향은 마지막 값, 궤적은 한 구간에 두 점, 이전 상태들은 바뀌지 않음(지도 그리기가 참조 비교에 의존) | `hmiState.test.ts > REQ_HMI_001 telemetry updates position and appends to the track` |
| TC-HMI-001-02 | U | telemetry 3001개 반영 | 3000점까지 유지, 넘으면 가장 오래된 점부터 한 묶음(300점) 삭제, 최신 점은 남음 | `hmiState.test.ts > REQ_HMI_001 track never exceeds TRACK_MAX_POINTS and drops the oldest points in blocks` |
| TC-HMI-001-03 | D | SIM 10Hz 송신 중 화면 확인 | 차량 표시 1개, 화살표가 heading(87.25°)만큼 회전(`data-heading`, `rotate(87.25deg)`), Leaflet 궤적 레이어에 실제로 들어 있는 점 수(개발 서버 훅 `__ugvcTrackPoints`)가 3s 동안 20개 이상 늘어남, 화면 캡처 | `hmi/e2e/smoke.mjs` 2단계 |
| TC-HMI-001-04 | U | 위치 유효 → GPS_VALID 없는 telemetry(lat=lon=0) | 위치·궤적은 마지막 유효 위치 그대로, 모드 등 다른 값은 최신 | `hmiState.test.ts > REQ_HMI_001 position without GPS_VALID does not move the vehicle or the track` |
| TC-HMI-001-05 | U | GPS 무효 10표본(약 1초), 차량 LOST, IF-03 연결 끊김 뒤의 유효 위치 | 각각 새 구간 시작 (끊긴 곳을 직선으로 잇지 않음) | `hmiState.test.ts > REQ_HMI_001 track starts a new segment after GPS loss, vehicle LOST or GW disconnect` |
| TC-HMI-001-06 | U | 연속된 apply() 결과로 그리기 연산, 무작위 8000메시지(GPS 무효 20%, 연결 끊김 1%)에서 연산을 실행한 결과, 6000점 동안 전체 다시 그리기 횟수, 조각 나누기 | extend·newLine·clear를 구분, 실행 결과를 끝점 공유로 이으면 상태 궤적과 같음, 조각 ≤200점, clear는 앞쪽 삭제 때만(301점마다) | `trackSync.test.ts > REQ_HMI_001 plans extend, new line and rebuild from successive apply() results`, `trackSync.test.ts > REQ_HMI_001 executing the planned ops always draws exactly the state track`, `trackSync.test.ts > REQ_HMI_001 rebuilds only when the oldest points are trimmed`, `trackSync.test.ts > REQ_HMI_001 chunks share end points so the drawn line has no gaps` |
| TC-HMI-001-07 | U | GPS_VALID가 한 표본씩 번갈아 40개 | 구간 하나에 유효 위치 20점 (점 하나짜리 구간이 생기지 않아 궤적이 그려짐) | `hmiState.test.ts > REQ_HMI_001 brief GPS_VALID flicker keeps the segment so the track stays drawable` |
| TC-HMI-001-08 | U | HmiController에서 IF-03 연결이 실제로 끊겼다가(close 1006) 재접속 | 궤적이 두 구간 (기준선 재설정과 달리 진짜 끊김은 궤적을 끊음) | `hmiController.test.ts > REQ_HMI_001 a real IF-03 disconnect starts a new track segment` |
| TC-HMI-001-09 | U | HmiController에서 기준선 재설정 전후로 텔레메트리 seq가 약 30 건너뜀: (a) 재설정 접속이 3s 동안 열리지 않음 (b) 소켓은 바로 열렸지만 그 뒤 첫 텔레메트리 seq가 약 30 건너뜀(처리 적체를 버린 재설정과 같은 입력. 브라우저 쪽 적체를 버리는 동작 자체는 이 시험의 대상이 아님) | 두 경우 모두 궤적 두 구간 (빠지는 표본이 없으면 TC-HMI-006-16처럼 한 구간) | `hmiController.test.ts > REQ_HMI_001 a resync connection that opens after 3s breaks the track`, `hmiController.test.ts > REQ_HMI_001 a resync connection that opens at once but skips about 30 seq breaks the track` |
| TC-HMI-001-10 | U | 유효 위치 사이 텔레메트리 seq 간격: 9개 빠짐, 10개 빠짐, 무효 1 + 빠짐 9, u32 순환(2개·25개 빠짐), seq 역행(500 → 3), 같은 seq 재수신 | 9개는 한 구간, 10개 이상은 두 구간 (GPS 무효와 빠진 표본을 같이 셈), 순환을 올바로 셈, 역행은 두 구간, 같은 seq는 점을 다시 붙이지 않음 | `hmiState.test.ts > REQ_HMI_001 a gap of 10 or more telemetry seq numbers breaks the track, 9 does not` |

### 3.11 REQ-HMI-002 상태 패널

**후속 범위:** REQ-HMI-002의 "지연 시간" 표시는 아직 TC가 없다. 영상-탐지 종단 지연(TBD-01) 측정과 함께 추가한다. RTM에서 이 부분은 미검증으로 보인다.

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-HMI-002-01 | U | 초기 상태 | 두 송신원 모두 링크 상태 없음(GW가 link_status를 보내기 전, 화면 "—"), 차량 정보 없음 | `hmiState.test.ts > REQ_HMI_002 starts with no link state until the GW reports one` |
| TC-HMI-002-02 | U | VEHICLE link_status(DEGRADED)와 stats 반영 | VEHICLE만 바뀌고 DETECTOR는 그대로, 유실률 반영 | `hmiState.test.ts > REQ_HMI_002 link_status and stats update per source` |
| TC-HMI-002-03 | D | GW 접속 → SIM 송신 → SIM 중지 | 차량 링크 LOST → CONNECTED(유실률 0.0%, 경과 0.x초) → 3s 안에 LOST, 모드·배터리 표시 | `hmi/e2e/smoke.mjs` 1~3단계 |
| TC-HMI-002-04 | U | HMI 시계가 GW보다 5s 늦을 때 링크 경과 시간 | 0.8s (음수가 아님), 수신 전이면 "수신 없음" | `view.test.ts > REQ_HMI_002 link age uses the connection baseline, not the raw GW clock` |
| TC-HMI-002-05 | U | 배터리 색, 위치 표시 | BATTERY_LOW 플래그가 있을 때만 경고색, GPS_VALID가 없으면 "위치 무효" | `view.test.ts > REQ_HMI_002 battery color follows BATTERY_LOW and position shows invalid without GPS_VALID` |
| TC-HMI-002-07 | U | HmiController 화면 판정에서 VEHICLE link_status(GW 시계로 0.3s 전 수신)·텔레메트리반영 | 상태 패널 행이 배너와 같은 `present()` 판정에서 나옴: VEHICLE CONNECTED·유실 0.0%·"0.3s 전", DETECTOR "수신 없음", 차량 모드·배터리·속도·위치 문구 | `hmiController.test.ts > REQ_HMI_002 the status panel rows come from the same judgement as the banner` |

### 3.12 REQ-HMI-006 IF-03 연결 감시·재접속·데이터 신선도

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-HMI-006-01 | D | 실제 GW·HMI 연결 중 GW 종료, 같은 포트로 GW 재시작. 시계 차 0, +3s, −5s로 각각 실행 | 3s 안에 배너 bad("GW 연결 끊김 · N초 후 재접속" 또는 "GW 연결 끊김 · 재접속 중")와 패널 흐림, 재시작 후 8s 안에 배너 ok(새 stats로 FRESH), 패널 흐림 해제. 시계 차가 있어도 1단계부터 ok | `hmi/e2e/smoke.mjs` 4·5단계 (`--clock-skew 3000`, `--clock-skew -5000`) |
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
| TC-HMI-006-16 | U | 가짜 소켓·타이머·벽시계·단조 시계(ms 단위)로 HmiController 구동(10Hz telemetry, 1Hz stats, 250ms tick, 화면은 `onChange`로 받은 View): FRESH 연결에서 HMI 시계 +3s 계단 변화. 위상 두 가지를 조합: 계단에서 다음 stats까지(4가지), stats 도착에서 tick까지(1·125·249·250ms). 빠른 재설정 접속. 정상 연결 60s | 첫 늦은 stats부터 5000ms 이상 5250ms 미만(판정 주기 하나 안), 계단부터 5000~6250ms에 재접속 1회(CONNECTING resync). 경계는 코드 상수가 아니라 ICD 값. 배너 기록에 "기준선 재설정 중"이 있고 "GW 연결 끊김"이 없음. 새 연결 첫 stats로 FRESH, 궤적은 한 구간 그대로. 정상 연결에서는 재접속 0회 | `hmiController.test.ts > REQ_HMI_006 a clock step is recovered by resync 5 to 5.25s after the first late stats and at most 6.25s after the step`, `hmiController.test.ts > REQ_HMI_006 after a quick resync the new baseline is FRESH and the track stays one segment`, `hmiController.test.ts > REQ_HMI_006 on-time data never triggers a resync` |
| TC-HMI-006-17 | U | HMI 시계가 GW보다 약 5s 늦은 연결에서 GW 시각 변환, 연결 없음 | GW 시각 + 기준선, 연결이 없으면 null | `hmiState.test.ts > REQ_HMI_006 GW time converts to HMI time through the connection baseline` |
| TC-HMI-006-18 | U | 첫 접속, 실패 뒤 재접속, 기준선 재설정 | CONNECTING의 cause가 first, retry(failures 1), resync | `gwConnection.test.ts > REQ_HMI_006 connect attempts say why: first, retry with the failure count, resync` |
| TC-HMI-006-19 | U | 재접속 대기(1s) 중 벽시계 +3s, −60s, 단조 시계 0.6s·1.0s 진행 | 벽시계 변화와 무관하게 "1초 후 재접속", 단조 시계로만 줄어 "0초 후 재접속" | `hmiController.test.ts > REQ_HMI_006 retry countdown follows the monotonic clock, not wall clock changes` |
| TC-HMI-006-20 | U | 제때 온 stats 뒤 `ts`가 큰 telemetry 하나로 기준선이 낮아짐, 이어서 stats | telemetry 직후 화면은 STALE이지만 재설정 시계는 시작하지 않음, 다음(늦은) stats 수신 시각부터 5000ms에 재설정 요청(4999ms에는 아님) | `hmiState.test.ts > REQ_HMI_006 the resync clock starts at the first late stats, not at a message that lowered the baseline` |

### 3.13 REQ-HMI-007 IF-03 수신 검증

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-HMI-007-01 | U | `hmi/tests/fixtures/gw_if03_sample.jsonl`(실제 GW 출력, `capture_gw_if03.py`로 생성) 파싱 | 4개 type 모두 받아들임 | `if03.test.ts > REQ_HMI_007 accepts every message type the real GW printed` |
| TC-HMI-007-02 | U | 정의 밖 필드가 붙은 telemetry | ICD 필드만 남김 | `if03.test.ts > REQ_HMI_007 keeps exactly the fields the ICD defines` |
| TC-HMI-007-03 | U | 깨진 JSON, 모르는 type, 빠진 필드, 정의 밖 열거값(mode, flags, state, source, class), ICD 6장 범위 밖 값(배터리 101·50.5, 위도 91, 방향 −1·360, 속도 30.01·1e308, seq·frame_id 2^32, img_w 0·4097, 객체 33개, w 0, x = img_w, x+w > img_w, y+h > img_h, conf 1.01, 유실률 1.5), errors 키 누락·추가·음수. `capture_ts` 1e300·2^70, `last_rx_ts`가 null인데 state CONNECTED. 경계 안 값(방향 359.99, 속도 30, seq 2^32−1, 상자가 이미지 끝에 닿음, 객체 32개, `capture_ts` 2^53+2·2^64, `last_rx_ts` null인 LOST) | 범위 밖은 모두 null, 경계 안은 받아들임 | `if03.test.ts > REQ_HMI_007 rejects malformed JSON, unknown types and missing fields`, `if03.test.ts > REQ_HMI_007 rejects values outside the ICD ranges and enums` |
| TC-HMI-007-04 | U | 텍스트가 아닌 프레임 | 텍스트만 전달 | `gwConnection.test.ts > REQ_HMI_007 passes text frames through and ignores binary` |
| TC-HMI-007-05 | U | 받은 텍스트: 깨진 JSON, 다른 필드는 다 갖추고 위도만 91인 telemetry, 정상 stats. HmiController에 형식 오류 메시지 2개 | 폐기 수 2, 정상 메시지는 반영되고 폐기 수는 그대로. 화면 판정(`present().discarded`, 상태 패널 `data-testid="discarded"`가 그리는 값)도 2이고 범위 밖 위도는 반영되지 않음 | `hmiState.test.ts > REQ_HMI_007 text that is not IF-03 is discarded and counted`, `hmiController.test.ts > REQ_HMI_007 discarded IF-03 messages are counted for display in the status panel` |
| TC-HMI-007-06 | U | cmd_result: 정의 밖 필드, 형식 오류 거절(cmd_id null·attempts 0·REJECTED_INVALID), 결과 4종, cmd_id 2^32−1, req_id 63바이트. 틀린 것: req_id 빈 값·66바이트·숫자, 정의 밖 result, cmd_id 0·2^32·1.5·문자열, attempts 0·5, cmd_id null인데 TIMEOUT 또는 attempts 1, TIMEOUT인데 attempts 1·3, result 없음 | ICD 7.1 필드만 남겨 받아들이고 틀린 것은 모두 null | `if03.test.ts > REQ_HMI_007 cmd_result is accepted only in the ICD 7.1 shape` |

### 3.14 REQ-CMD-002 명령 송신·재전송·결과 (GW)

U는 GoogleTest(가짜 시계), I는 실제 GW·SIM(`python -m ugvc_sim`)과 WebSocket HMI(`tests/integration/test_cmd_relay.py`).

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-CMD-002-01 | U | 명령 접수 | 바로 송신(attempt 1), u32 순환 시 0을 건너뜀, 시작값은 0.1초 단위 시각(1초 뒤 다시 시작한 GW는 10 뒤부터) | `CommandTrackerTest.REQ_CMD_002_first_send_is_immediate_attempt_1`, `CommandTrackerTest.REQ_CMD_002_cmd_id_skips_0_on_wrap`, `CommandTrackerTest.REQ_CMD_002_cmd_id_start_follows_the_clock_so_a_restarted_gw_does_not_reuse_ids` |
| TC-CMD-002-02 | U | ACK 없이 999ms·1000ms | 999ms에는 재전송 없음, 1000ms에 같은 cmd_id로 재전송 | `CommandTrackerTest.REQ_CMD_002_resend_at_1000ms_not_before_with_same_cmd_id` |
| TC-CMD-002-03 | U | ACK가 끝내 없음 | 0·1·2·3s에 4번 송신, 3999ms에는 결과 없음, 4000ms에 TIMEOUT attempts 4, 다섯 번째 송신 없음 | `CommandTrackerTest.REQ_CMD_002_no_ack_sends_four_times_then_timeout_at_4s` |
| TC-CMD-002-04 | U | 결과 확정 전 ACK, ACK result 3종 | 해당 result로 확정(attempts = 송신 횟수), 이후 재전송 없음 | `CommandTrackerTest.REQ_CMD_002_ack_before_timeout_gives_accepted_with_attempts_and_stops_resending`, `CommandTrackerTest.REQ_CMD_002_ack_results_are_mapped` |
| TC-CMD-002-05 | U | 같은 req_id 재제출(대기 중, 결과 후 60s 안·같은 내용, 다른 내용, 60s 뒤), 기억 상한, 형식 오류 거절, 두 명령 동시, TIMEOUT 뒤 늦은 ACK | 대기 중이면 보내지 않고 기록, 결과 후 같은 내용이면 같은 결과를 다시 알림(재송신 없음), 다른 내용은 무시·기록, 60s 뒤나 256개 넘은 오래된 것은 새 명령, 형식 오류는 req_id가 쓰이는 중이면 무시·아니면 REJECTED_INVALID(n=0), 명령마다 타이머 독립, 늦은 ACK는 결과를 바꾸지 않음 | `CommandTrackerTest.REQ_CMD_002_same_req_id_while_pending_is_ignored_and_logged`, `CommandTrackerTest.REQ_CMD_002_resubmitted_req_id_gets_the_same_result_without_resending`, `CommandTrackerTest.REQ_CMD_002_recent_results_are_bounded`, `CommandTrackerTest.REQ_CMD_002_invalid_command_is_rejected_unless_its_req_id_is_in_use`, `CommandTrackerTest.REQ_CMD_002_concurrent_commands_have_independent_timers`, `CommandTrackerTest.REQ_CMD_002_late_ack_after_timeout_is_unknown_and_gives_no_result` |
| TC-CMD-002-06 | U | IF-03 command 파싱: STOP·RTB, ts 생략, WAYPOINT 도 → 1e-7 반올림·경계값, 범위 밖·누락·비유한 좌표, 정의 밖 cmd_type, req_id 누락·빈 값·65자, 깨진 입력 | 정상은 CommandRequest, 형식 오류는 거절(req_id를 읽었으면 함께 반환), 어떤 입력에도 예외 없음 | `If03Command.REQ_CMD_002_stop_and_rtb_ignore_coordinates`, `If03Command.REQ_CMD_002_ts_is_optional`, `If03Command.REQ_CMD_002_waypoint_converts_to_e7_with_rounding`, `If03Command.REQ_CMD_002_waypoint_accepts_range_limits_and_integers`, `If03Command.REQ_CMD_002_waypoint_out_of_range_or_missing_or_non_finite_is_rejected_with_req_id`, `If03Command.REQ_CMD_002_overflowing_or_nan_number_is_rejected`, `If03Command.REQ_CMD_002_unknown_or_non_string_cmd_type_is_rejected_with_req_id`, `If03Command.REQ_CMD_002_bad_req_id_is_rejected_without_req_id`, `If03Command.REQ_CMD_002_malformed_input_is_rejected_without_req_id` |
| TC-CMD-002-07 | U | cmd_result JSON, COMMAND 인코딩, cmd_type 이름 | ICD 7.1 형식(cmd_id 없으면 null, result 이름 4종), 공용 벡터와 같은 바이트 | `If03Command.REQ_CMD_002_cmd_result_json_shape`, `If03Command.REQ_CMD_002_cmd_result_without_cmd_id_is_null`, `If03Command.REQ_CMD_002_cmd_result_names_all_results`, `Codec.REQ_QUAL_001_command_matches_vectors`, `Codec.REQ_CMD_002_cmd_type_names` |
| TC-CMD-002-08 | I | SIM 정상 / 첫 COMMAND 유실(`--drop-first 1`) / SIM 없음 / WAYPOINT | ACCEPTED attempts 1(1s 안, stdout과 WS가 같은 결과) / 0.95~1.6s에 같은 cmd_id로 재전송해 attempts 2 / 3.95~4.6s에 TIMEOUT attempts 4 / WAYPOINT가 SIM에 cmd_type 3으로 한 번 실행 | `test_REQ_CMD_002_command_accepted_on_first_send`, `test_REQ_CMD_002_lost_first_send_is_retransmitted_after_1s`, `test_REQ_CMD_002_no_ack_times_out_after_three_retransmits`, `test_REQ_CMD_002_waypoint_is_relayed` |
| TC-CMD-002-09 | I | HMI가 범위 밖 WAYPOINT(위도 91) 송신 | SIM에 보내지 않고 0.5s 안에 cmd_result REJECTED_INVALID(cmd_id null, attempts 0) | `test_REQ_CMD_002_invalid_hmi_command_is_rejected_without_sending` |
| TC-CMD-002-10 | U | 판정이 40ms 늦은 재전송 뒤 이어지는 송신 | 다음 송신은 예정 시각(2000·3000ms)에, TIMEOUT은 4000ms에 (늦음이 쌓이지 않음) | `CommandTrackerTest.REQ_CMD_002_resend_schedule_does_not_drift_with_late_polls` |
| TC-CMD-002-12 | I | 시험의 송신기가 차량 포트로 TELEMETRY를 10Hz로 계속 보내 차량 링크를 CONNECTED로 둔 채 SIM만 다시 시작한 뒤 명령 | ACCEPTED attempts 1 (새 SIM의 ACK seq 0이 order로 버려지지 않음), 재시작 뒤 받은 차량 link_status 보고 2회가 모두 CONNECTED(LOST 뒤 기준값 재설정으로 통과한 것이 아님) | `test_REQ_CMD_002_restarted_sim_uplink_acks_are_accepted` |
| TC-CMD-002-13 | I | (a) 대기 중인 req_id로 형식이 틀린 명령 (b) HMI A의 명령이 대기 중일 때 HMI B가 같은 req_id로 다른 명령 | (a) 그 req_id의 결과는 ACCEPTED 하나뿐, 무시 기록 (b) B의 명령은 SIM에 가지 않고 무시 기록, A·B 모두 같은 결과 하나를 받음(모든 HMI 전달) | `test_REQ_CMD_002_invalid_command_with_pending_req_id_does_not_give_a_second_result`, `test_REQ_CMD_002_same_req_id_from_another_hmi_is_not_sent_and_both_hmis_get_the_result` |
| TC-CMD-002-14 | I | 결과를 받은 HMI가 다시 접속해 같은 req_id·같은 WAYPOINT를 다시 보냄 | 0.5s 안에 같은 cmd_result(같은 cmd_id)를 다시 받고, SIM 실행은 한 번 | `test_REQ_CMD_002_hmi_resending_after_missing_the_result_does_not_execute_twice` |
| TC-CMD-002-15 | U | 형식 오류로 거절한 req_id로 올바른 명령, 같은 형식 오류 다시, 60초 뒤 | 올바른 명령은 보내지 않고 무시 기록, 같은 형식 오류에는 같은 거절을 다시(repeated), 60초 뒤에는 새 명령으로 보냄 | `CommandTrackerTest.REQ_CMD_002_rejected_req_id_is_remembered_so_it_has_one_result` |
| TC-CMD-002-16 | U | 첫 판정이 3.5s 늦음(io 스레드 정지) | 그때 한 번만 재전송하고 4.45·5.45s에 다음 송신(실제 송신 + 0.95s), 마지막 송신 1.0s 뒤(6.45s)에 TIMEOUT (밀린 재전송을 몰아 보내지 않음) | `CommandTrackerTest.REQ_CMD_002_resend_after_a_poll_stall_still_waits_for_ack` |
| TC-CMD-002-17 | I | HMI가 req_id `X`로 위도 91 WAYPOINT, 0.3s 뒤 같은 `X`로 고친 좌표, 다시 위도 91 | 결과는 REJECTED_INVALID 두 번(두 번째는 repeated)뿐, SIM에 COMMAND 없음, 고친 명령은 무시 기록 | `test_REQ_CMD_002_corrected_command_with_a_rejected_req_id_is_not_sent` |
| TC-CMD-002-18 | U | 2번째 송신 판정이 0.9s 늦음(1.9s) | 3번째 송신은 2.0s가 아니라 2.85s(1.9 + 0.95), 4번째 3.85s, TIMEOUT 4.85s (ACK 대기가 줄지 않음) | `CommandTrackerTest.REQ_CMD_002_resend_0_9s_late_does_not_shorten_the_next_wait` |
| TC-CMD-002-19 | U | (a) ACCEPTED 뒤 서로 다른 req_id의 형식 오류 257개, 같은 명령 재제출 (b) 형식 오류 거절 뒤 송신 명령 257개, 거절한 req_id로 고친 명령 | (a) 다시 보내지 않고 같은 결과를 다시 알림 (b) 보내지 않고 무시 기록 (두 기억의 상한이 서로 밀어내지 않음) | `CommandTrackerTest.REQ_CMD_002_rejects_do_not_push_out_results_of_sent_commands`, `CommandTrackerTest.REQ_CMD_002_sent_commands_do_not_push_out_rejects` |

### 3.15 REQ-CMD-003 중복 cmd_id 재실행 금지 (SIM)

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-CMD-003-01 | U | 같은 cmd_id 두 번(수락된 것, 거절된 것) | 두 번째는 같은 ACK, 실행하지 않음 | `test_REQ_CMD_003_duplicate_cmd_id_resends_same_ack_without_executing`, `test_REQ_CMD_003_duplicate_of_rejected_command_resends_rejection` |
| TC-CMD-003-02 | U | 서로 다른 cmd_id, 기억 상한 넘김 | 서로 독립, 가장 오래된 cmd_id부터 잊음 | `test_REQ_CMD_003_different_cmd_ids_are_independent`, `test_REQ_CMD_003_memory_is_bounded_and_forgets_oldest` |
| TC-CMD-003-03 | U | ACK 프레임 | ACK(5B)로 검증 통과, ACK 스트림 seq 0·1… | `test_REQ_CMD_003_ack_frame_is_valid_and_seq_increments` |
| TC-CMD-003-04 | U | 실제 UDP로 SIM 본체(`app.run`) 구동: 중복 cmd_id, 첫 COMMAND 무시(`--drop-first 1`) | 같은 ACK 두 번·executed true→false, 무시한 뒤 재전송에 ACK | `test_REQ_CMD_003_listen_duplicate_gets_same_ack`, `test_REQ_CMD_003_drop_first_ignores_first_command_then_acks_retransmission` |
| TC-CMD-003-05 | I | SIM의 ACK가 GW에 닿지 않음(다른 포트로 보냄) | GW는 같은 cmd_id로 4번 보내고 TIMEOUT, SIM은 4번 모두 같은 ACCEPTED ACK, 실행은 처음 한 번 | `test_REQ_CMD_003_retransmitted_command_executes_once` |
| TC-CMD-003-06 | I | SIM을 띄운 채 GW를 다시 시작하고 새 명령 | 새 cmd_id가 앞 GW의 것과 다르고 SIM이 두 명령 모두 실행 | `test_REQ_CMD_003_restarted_gw_does_not_reuse_cmd_ids` |

### 3.16 REQ-CMD-005 명령 이벤트 기록 (GW)

여기서는 GW 진단 기록(stderr)에 남는 것을 확인한다. 같은 사건의 PostgreSQL 저장은 TC-LOG-001-04·08(3.24)이 확인한다.

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-CMD-005-01 | U | 송신 → 재전송 → 결과 | 이벤트가 일어난 순서대로 나오고, 꺼내면 비워짐 | `CommandTrackerTest.REQ_CMD_005_events_in_order_sent_resent_result` |
| TC-CMD-005-02 | U | 중복 ACK, 발급하지 않은 cmd_id의 ACK, 정의 밖 result의 ACK | UnknownAck 이벤트만, 결과 없음, 대기 중 명령은 그대로 | `CommandTrackerTest.REQ_CMD_005_duplicate_ack_is_unknown`, `CommandTrackerTest.REQ_CMD_005_ack_with_unassigned_cmd_id_is_unknown_and_keeps_pending`, `CommandTrackerTest.REQ_CMD_005_ack_with_undefined_result_is_unknown_and_keeps_pending` |
| TC-CMD-005-03 | I | 첫 송신 유실 뒤 재전송으로 수락. req_id에 개행을 넣은 명령 | stderr에 `cmd sent` attempt 1·2와 `cmd result` ACCEPTED attempts 2. req_id는 JSON 문자열로 기록되어 가짜 진단 줄이 생기지 않음 | `test_REQ_CMD_005_sends_retransmits_and_result_are_logged`, `test_REQ_CMD_005_req_id_cannot_forge_diagnostic_lines` |

### 3.17 REQ-CMD-006 범위 밖 COMMAND (SIM)

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-CMD-006-01 | U | cmd_id 0 | 폐기(range), ACK 없음 | `test_REQ_CMD_006_cmd_id_0_is_dropped` |
| TC-CMD-006-02 | U | 정의 밖 cmd_type, WAYPOINT 좌표 범위 밖 | 별도 형(InvalidCommand)으로 해석, REJECTED_INVALID ACK, 실행하지 않음 | `test_REQ_CMD_006_out_of_range_command_is_parsed_as_invalid`, `test_REQ_CMD_006_acks_rejected_invalid`, `test_REQ_CMD_006_invalid_command_is_not_executed` |
| TC-CMD-006-03 | U | 좌표 경계(±90°, ±180°), 정상 명령 | 수락·실행 | `test_REQ_CMD_006_waypoint_bounds_accepted`, `test_REQ_CMD_006_valid_command_accepted_and_executed` |
| TC-CMD-006-04 | U | 실제 UDP: 범위 밖 명령, cmd_id 0 | REJECTED_INVALID ACK / ACK 없음 | `test_REQ_CMD_006_listen_rejects_invalid_over_udp`, `test_REQ_CMD_006_listen_cmd_id_0_gets_no_ack` |

### 3.18 REQ-SAFE-001 HEARTBEAT 3.0s 두절 시 Failsafe 정지 (SIM)

U 수준은 `Simulator`에 시각을 직접 넣는다(2.2). 판정 경계는 "3.0초 이상"이다.

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-SAFE-001-01 | U | 시작 후 HEARTBEAT 없음, 2.999s와 3.000s | 2.999s는 MISSION·플래그 없음, 3.000s에 STOPPED, speed 0, FAILSAFE_ACTIVE (시작 시각부터 잼) | `test_REQ_SAFE_001_no_heartbeat_since_start_failsafe_at_3s` |
| TC-SAFE-001-02 | U | 마지막 HEARTBEAT 뒤 +2.999s, +3.000s | +3.000s에 Failsafe (마지막 HEARTBEAT부터 잼) | `test_REQ_SAFE_001_failsafe_3s_after_last_heartbeat` |
| TC-SAFE-001-03 | U | 두절 직전 mode가 MISSION(경로 있음·없음), RTB, STOPPED, STANDBY | 모두 STOPPED + FAILSAFE_ACTIVE | `test_REQ_SAFE_001_failsafe_from_any_mode` |
| TC-SAFE-001-04 | U | Failsafe 중 주기 진행, 두절이 이어짐 | 위치 고정, FAILSAFE 플래그 변화는 3.0s에 한 번뿐 | `test_REQ_SAFE_001_vehicle_does_not_move_in_failsafe`, `test_REQ_SAFE_001_failsafe_is_a_single_transition_per_outage` |
| TC-SAFE-001-05 | U | 명령으로 재개한 뒤 두 번째 두절 | 다시 Failsafe | `test_REQ_SAFE_001_failsafe_again_after_second_outage` |
| TC-SAFE-001-06 | U | 실제 SIM 본체(`app.run`)(실제 UDP·시계), HEARTBEAT 없음 | 첫 Failsafe TELEMETRY가 시작 후 3.0~3.35s, 그 전은 MISSION·플래그 없음, 뒤로는 위치 고정 | `test_REQ_SAFE_001_process_without_heartbeat_reports_failsafe` |
| TC-SAFE-001-07 | U | SIM 본체(`app.run`)의 송신이 모두 OSError(GW 경로 없음, 가짜 소켓·가짜 시계) | SIM이 죽지 않고 40개 주기(0.0~3.9s)를 모두 돎. TELEMETRY seq 0~29(≤2.9s)는 FAILSAFE_ACTIVE 없음, seq 30(3.0s)부터 STOPPED·speed 0·FAILSAFE_ACTIVE, Failsafe 전이 기록 1회. 1.05s에 받은 COMMAND의 ACK 송신도 실패하지만 계속 돎. 송신 오류 기록은 실패 시작에 한 줄 | `test_REQ_SAFE_001_send_failure_does_not_kill_sim_and_failsafe_still_engages` |
| TC-SAFE-001-08 | U | SIM 본체(`app.run`) 루프가 1s 무렵 3.5s 멈췄다 재개(가짜 시계 건너뜀) | 멈춘 뒤 첫 TELEMETRY가 이미 Failsafe, 그 전은 플래그 없음, 밀린 주기를 같은 시각에 몰아 보내지 않음(간격 ≥ 0.1s) | `test_REQ_SAFE_001_stalled_loop_sends_one_failsafe_telemetry_without_burst` |

### 3.19 REQ-SAFE-002 링크 복구 뒤 운용자 명령 없이 재개 금지 (SIM)

해제 규칙은 ICD 6.5 표를 따른다.

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-SAFE-002-01 | U | Failsafe 뒤 HEARTBEAT가 10s 동안 다시 옴. 3s 넘게 끊긴 뒤 처음 온 HEARTBEAT | STOPPED + FAILSAFE_ACTIVE 유지, 위치 고정. 처음 온 HEARTBEAT도 먼저 Failsafe로 들어감 | `test_REQ_SAFE_002_heartbeat_recovery_does_not_resume`, `test_REQ_SAFE_002_heartbeat_arriving_after_3s_gap_still_enters_failsafe` |
| TC-SAFE-002-02 | U | 복구 뒤 RTB, WAYPOINT | ACCEPTED, 플래그 해제, RTB·MISSION으로 주행 | `test_REQ_SAFE_002_motion_command_after_recovery_resumes_and_clears_flag` |
| TC-SAFE-002-03 | U | 복구 뒤 STOP | ACCEPTED, 플래그 해제, STOPPED 유지 | `test_REQ_SAFE_002_stop_after_recovery_clears_flag_stays_stopped` |
| TC-SAFE-002-04 | U | 두절 중 RTB, WAYPOINT, STOP, 범위 밖 명령 | RTB·WAYPOINT는 REJECTED_STATE·실행 안 함·위치 고정, STOP은 ACCEPTED·플래그 유지, 범위 밖은 REJECTED_INVALID | `test_REQ_SAFE_002_motion_command_during_outage_rejected_state`, `test_REQ_SAFE_002_invalid_command_during_outage_is_rejected_invalid` |
| TC-SAFE-002-05 | U | 두절 중 REJECTED_STATE를 받은 cmd_id를 복구 뒤 다시, 이어서 새 cmd_id | 같은 cmd_id는 REJECTED_STATE 그대로·정지 유지, 새 cmd_id는 ACCEPTED·mode RTB·플래그 해제 | `test_REQ_SAFE_002_rejected_state_cmd_id_retransmitted_after_recovery_keeps_result` |
| TC-SAFE-002-06 | U | 실제 SIM 본체(`app.run`): Failsafe 뒤 HEARTBEAT 1.5s, 이어서 COMMAND RTB | HEARTBEAT 구간의 TELEMETRY 전부 정지·플래그·위치 고정, RTB에 ACK ACCEPTED, TELEMETRY가 RTB·플래그 없음 | `test_REQ_SAFE_002_process_resumes_only_on_command` |
| TC-SAFE-002-07 | I | 실제 GW·SIM·HMI(WebSocket): GW를 3.8s 내렸다가 같은 포트로 재시작, 이어서 HMI RTB 명령 | 재시작 뒤 1.6s 동안 IF-03 telemetry가 모두 STOPPED·speed 0·FAILSAFE_ACTIVE, RTB에 cmd_result ACCEPTED, telemetry가 RTB·플래그 없음 | `test_REQ_SAFE_002_failsafe_after_gw_outage_resumes_on_hmi_command` |
| TC-SAFE-002-08 | U | Failsafe 뒤 복구 HEARTBEAT 한 번, 그 뒤 +2.999s와 +3.000s에 RTB | +2.999s는 ACCEPTED·플래그 해제·RTB, +3.000s는 REJECTED_STATE·정지 유지 | `test_REQ_SAFE_002_release_boundary_is_3s_after_recovery_heartbeat` |
| TC-SAFE-002-09 | U | 출발 지점을 기본값이 아닌 점으로 준 `Simulator`, Failsafe·복구 뒤 RTB | ACCEPTED, 그 점에 도착해 STANDBY | `test_REQ_SAFE_002_rtb_after_recovery_drives_to_simulator_home` |

### 3.20 REQ-CMD-001 운용자 확인 뒤에만 명령 송신 (HMI)

U는 vitest(가짜 소켓·타이머·시계, `HmiController`와 순수 함수 `state/commands.ts`), D는 시연 점검(실제 GW·SIM(`python3 -m ugvc_sim`, 명령에 ACK)·HMI). 실제 송신 여부는 화면이 아니라 GW 진단 기록의 `cmd sent` 줄로 판정한다.

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-CMD-001-01 | U | STOP·RTB·WAYPOINT command 텍스트 | ICD 7.2 봉투(`type` command, `data`에 req_id·cmd_type), 좌표는 WAYPOINT만 | `commands.test.ts > REQ_CMD_001 command text follows ICD 7.2 with coordinates only for WAYPOINT` |
| TC-CMD-001-02 | U | 세 버튼 각각 누르고 취소, 확인할 명령 없이 확인, WAYPOINT 확인 두 번 | 버튼·취소만으로는 송신 0, 확인 단계에 명령 문구(좌표 7자리), 확인하면 한 번만 송신하고 확인 단계가 끝남, 기록은 PENDING | `hmiController.test.ts > REQ_CMD_001 a command button only opens confirmation, cancel sends nothing, confirm sends once` |
| TC-CMD-001-03 | U | 확인할 명령 없이 송신 기록, 취소 뒤 송신 기록, 결과 전에 25개 송신 후 끊김, 그 뒤 결과를 받고 하나 더 송신 | draft 없으면 기록 없음. 결과를 받지 못한 25개(결과 미확인)는 모두 남고 첫 명령의 늦은 결과도 맞춰짐(상한 20을 넘어도 밀리지 않음). 결과를 받은 것부터 지워져 20개, 최근 것부터 | `commands.test.ts > REQ_CMD_001 a sent record exists only for a confirmed draft and commands without a result are never pushed out` |
| TC-CMD-001-04 | U | 명령 셋 송신, 결과 ACCEPTED(2회)·TIMEOUT(4회)·REJECTED_INVALID(cmd_id null), 다른 HMI의 req_id 결과, 같은 req_id 결과 재전달 | req_id가 명령마다 다름, 결과 문구·등급 표시, 다른 HMI의 결과와 재전달은 바꾸지 않음(폐기도 아님) | `hmiController.test.ts > REQ_CMD_001 each confirmed command has its own req_id and shows its cmd_result`, `commands.test.ts > REQ_CMD_001 results match our req_id once, a disconnect leaves pending commands unconfirmed and closes the confirmation` |
| TC-CMD-001-05 | U | 결과 전에 IF-03 끊김(close 1006) 뒤 재접속, 새 연결에서 10s, 이어서 새 연결로 결과. 확인 단계가 열린 채 끊김 | 처음 한 번 말고는 송신 없음(새 소켓으로 보낸 것 0), 기록은 곧바로 "결과 미확인 · 이미 실행됐을 수 있음 (자동으로 다시 보내지 않음)", 새 연결로 결과가 오면 결과로 바뀜, 확인 단계는 닫힘 | `hmiController.test.ts > REQ_CMD_001 a command whose result was not received is never resent automatically after a disconnect`, `commands.test.ts > REQ_CMD_001 results match our req_id once, a disconnect leaves pending commands unconfirmed and closes the confirmation` |
| TC-CMD-001-06 | U | RTB 결과 대기 중에 RTB 확인, 끊겨 결과 미확인인 RTB가 있을 때 RTB 확인 후 보내기, 다른 종류(WAYPOINT)·STOP 확인, 결과를 받은 뒤 확인 | 같은 종류가 결과 전이면 확인 단계에 경고 "앞서 보낸 복귀 (RTB)의 결과를 아직 받지 못함 · 이미 실행됐을 수 있어 보내면 두 번 실행될 수 있음", 보내면 새 req_id(r1 → r2). 다른 종류·STOP·결과를 받은 뒤에는 경고 없음. 더 새 명령의 결과로 숨기는 것은 오래된 결과 미확인뿐(결과 미확인 r2 뒤 r3가 결과를 받으면 경고 없음, 결과 대기 p1 뒤 p2가 결과를 받아도 p1 경고) | `hmiController.test.ts > REQ_CMD_001 trying again is a new confirmed command with a new req_id, warned while the earlier one has no result`, `commands.test.ts > REQ_CMD_001 confirmation warns about an unresolved command of the same kind except STOP` |
| TC-CMD-001-07 | U | 경로점 입력: 공백 둘레, 경계(90, −180), `.5`·`+1.`, 소수 7자리. 틀린 것: 빈 값, `1e1`, `0x10`, NaN, Infinity, `1,5`, 90.0000001, 180.5, −91, 소수 8자리(36.00000015) | 소수 7자리 이하 십진수 도 단위로 ICD 7.2 범위 안만 WAYPOINT, 나머지는 오류 문구(8자리는 확인 화면과 GW가 싣는 1e-7 정수가 다를 수 있음) | `commands.test.ts > REQ_CMD_001 waypoint input accepts decimal degrees within ICD 7.2 ranges only` |
| TC-CMD-001-08 | D | SIM 가동 중 정지 버튼 → 취소, 정지 → 보내기, 경로점(36.5, 127.25) → 보내기 | 취소 뒤 1.5s 동안 GW `cmd sent` 0, 보내기마다 결과 ACCEPTED "수락 (송신 1회)", GW가 보낸 req_id 순서가 화면 기록과 같음, 화면 캡처 | `hmi/e2e/smoke.mjs` 2c단계 |
| TC-CMD-001-09 | D | SIM을 멈춰 차량 LOST를 거친 뒤 `--drop-first 100`(COMMAND를 ACK 없이 버림)으로 다시 띄우고 복귀 송신, 결과 전에 GW 중지 → 같은 포트로 재시작 → 패널의 복귀 → 보내기 | 결과 대기 행은 "결과 대기 중 · N초", 끊기면 "결과 미확인", 새 GW는 2s 동안 `cmd sent` 0, 복귀 확인 단계에 같은 종류 경고(송신 0), 보내면 새 GW가 이전과 다른 req_id 하나로 송신(재전송 줄은 req_id별 하나로 셈), 6s 안에 "응답 없음 (송신 4회)" | `hmi/e2e/smoke.mjs` 2d단계 |
| TC-CMD-001-10 | U | 시작 전·접속 중·열린 뒤·소켓 예외·끊김 뒤·재접속 뒤·정지 뒤 `send()` | 열린 소켓에만 보내고 true, 그 밖은 false. 끊긴 뒤 새 소켓에 이전 텍스트를 다시 보내지 않음(대기열 없음) | `gwConnection.test.ts > REQ_CMD_001 send() writes only to an open socket and never queues or resends` |
| TC-CMD-001-11 | U | (a) FRESH 연결에서 결과가 첫 송신 30s 뒤에 옴(GW 늦은 판정·IF-03 지연) (b) 기준선 재설정(HMI 시계 +3s 계단 변화) 직전에 RTB 송신, 재설정 뒤 새 연결에서 10s, 이어서 결과 | (a) 결과까지 "결과 대기 중 · 30초", 시간으로 상태가 바뀌지 않음, 결과가 오면 ACCEPTED (b) 재설정되면 결과 미확인, 새 연결 뒤에 확정된 결과가 오면 ACCEPTED. 두 경우 모두 송신 한 번 | `hmiController.test.ts > REQ_CMD_001 a command keeps waiting for its result however late it comes, and a result after a resync still counts` |
| TC-CMD-001-12 | U | 결과 미확인 RTB가 있을 때 RTB 확인 단계(경고) 중 원래 명령의 늦은 ACCEPTED, 이어서 보내기. 다른 종류(STOP) 결과는 RTB 확인 단계 중에 도착 | 경고한 명령의 결과가 오면 확인 단계가 닫혀 보내기 송신 0(이중 실행 없음), 행은 ACCEPTED. 경고와 무관한 결과는 확인 단계를 닫지 않음 | `hmiController.test.ts > REQ_CMD_001 a late result of the warned command closes the open confirmation so nothing is sent on it` |
| TC-CMD-001-13 | U | 기준선 재설정 직전에 RTB 송신, 그 결과는 재설정이 버린 적체 안이라 오지 않음. 새 연결에서 2분(FRESH), RTB 확인, 보내기(r2), r2 ACCEPTED 뒤 RTB 확인 / 재설정 직전에 RTB 확인 단계를 열어 둠, 재설정 뒤 새 연결의 link_status 다음 보내기 | 결과 대기로 남지 않고 "결과 미확인 · 이미 실행됐을 수 있음 (자동으로 다시 보내지 않음)", 그 뒤 RTB 확인에는 경고, 더 새 RTB가 결과를 받은 뒤에는 경고 없음. 송신은 r1·r2뿐 / 확인 단계가 닫히고 송신 0 | `hmiController.test.ts > REQ_CMD_001 a result lost in the backlog dropped by a baseline resync ends as unconfirmed, and a newer result of the same kind ends the warning`, `hmiController.test.ts > REQ_CMD_001 a baseline resync closes an open confirmation so nothing is sent on it` |

### 3.21 REQ-CMD-004 차량 링크 LOST면 명령 비활성 (HMI)

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-CMD-004-01 | U | 접속 전, 열린 직후(첫 link_status 전), LOST, CONNECTED, 확인 단계 중 LOST, 다시 CONNECTED, 확인 단계 중 IF-03 끊김 | 사유 "GW와 연결되지 않아 명령을 보낼 수 없음" / "차량 링크 상태 수신 전 · 명령을 보낼 수 없음" / "차량 링크 LOST · 명령을 보낼 수 없음" / 없음. LOST 동안 확인해도 송신 0(확인 단계는 남음), CONNECTED로 돌아오면 확인으로 송신. 끊기면 확인 단계가 닫히고 송신 0 | `hmiController.test.ts > REQ_CMD_004 vehicle link LOST or no GW connection disables commands with the reason and confirm sends nothing` |
| TC-CMD-004-02 | D | SIM 중지(차량 LOST), GW 중지 | 정지·복귀·경로점 버튼 비활성, 사유 문구 "차량 링크 LOST · 명령을 보낼 수 없음" / "GW와 연결되지 않아 명령을 보낼 수 없음" | `hmi/e2e/smoke.mjs` 3·4단계 |
| TC-CMD-004-03 | U | 차량 CONNECTED인 연결이 (a) 끊겼다가 재접속 (b) 기준선 재설정 재접속. 새 연결의 첫 link_status 전에 확인, 이어서 LOST 수신 | 이전 연결의 CONNECTED로 보내지 않음: 사유 "차량 링크 상태 수신 전 · 명령을 보낼 수 없음", 송신 0, LOST를 받으면 LOST 사유 | `hmiController.test.ts > REQ_CMD_004 after a reconnect commands stay disabled until the new connection reports the vehicle link` |

### 3.22 REQ-HMI-003 경고 3단계 표시·확인 (HMI)

U는 vitest(순수 함수 `state/alerts.ts`, `HmiController`), D는 시연 점검. 기대값은 SRS 표 3-1(등급)·표 3-2(수명)에서 옮긴다(코드의 등급 표를 가져오지 않는다). 화면은 경고마다 `data-testid="alert"`에 `data-kind`(조건 종류)·`data-level`·`data-state`(ACTIVE, CLEARED 해제됨, ACKED 확인됨)를 두고, WARNING·CAUTION은 배너 아래(`alerts-urgent`), ADVISORY는 오른쪽 패널 위(`alerts-advisory`)에 그린다.

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-HMI-003-01 | U | WARNING 조건 성립 → 확인 전에 풀림 → 확인 | ACTIVE → CLEARED(확인 버튼 있음) → 사라짐 | `alerts.test.ts > REQ_HMI_003 a warning that clears before acknowledgement stays as cleared until acknowledged` |
| TC-HMI-003-02 | U | CAUTION 조건이 이어지는 중에 확인 → 조건 풀림 | ACKED(확인 버튼 없음)로 남음 → 사라짐 | `alerts.test.ts > REQ_HMI_003 acknowledging while the condition lasts keeps it as acknowledged until the condition clears` |
| TC-HMI-003-03 | U | 해제됨 동안 같은 조건 재발 / 확인해 사라진 뒤 재발 / 같은 조건 집합으로 다시 판정 | 같은 경고(같은 번호)가 다시 ACTIVE / 새 번호의 새 경고 ACTIVE / 목록 그대로(멱등) | `alerts.test.ts > REQ_HMI_003 a condition back while cleared reactivates the same alert, after it is gone it is a new alert` |
| TC-HMI-003-04 | U | ADVISORY 조건 성립, 확인 시도, 풀림 | 확인 버튼 없음, 확인해도 그대로, 풀리면 바로 사라짐 | `alerts.test.ts > REQ_HMI_003 an advisory needs no acknowledgement and goes away with its condition` |
| TC-HMI-003-05 | U | 결과 미확인 명령 둘(r1·r2), r1의 결과가 늦게 옴(조건 사라짐), r2는 계속 결과 미확인인 채 확인, r1 확인 | 명령마다 경고 1개, r1 경고는 풀리지 않고 ACTIVE, 확인한 r2는 조건이 이어져도 다시 뜨지 않음, r1도 확인하면 사라지고 목록에 남는 것 없음 | `alerts.test.ts > REQ_HMI_003 command result alerts are one per command, do not clear, and are gone for good once acknowledged` |
| TC-HMI-003-06 | U | 등급이 섞인 경고 다섯 개를 두 차례에 걸쳐 | WARNING → CAUTION → ADVISORY 순, 같은 등급은 최근 것부터 | `alerts.test.ts > REQ_HMI_003 rows are WARNING, then CAUTION, then ADVISORY, newest first within a level` |
| TC-HMI-003-07 | U | HmiController에서 차량 LOST 경고를 `acknowledgeAlert`로 확인, 이어서 CONNECTED | ACKED → 사라짐 | `hmiController.test.ts > REQ_HMI_003 the operator acknowledges an alert through the controller` |
| TC-HMI-003-08 | D | 실제 GW·SIM·HMI: GW 접속(SIM 없음) → SIM 송신 → 확인 / 시계 +3s 계단 변화로 기준선 재설정 → 확인 / 결과 전 GW 재시작과 TIMEOUT → 명령 결과 경고 확인 | 차량 LOST WARNING이 배너 아래에 ACTIVE → CLEARED → 확인 버튼으로 사라짐 / GW 지연 CAUTION이 CLEARED로 남고 GW 끊김 경고 없음 / 결과 미확인·명령 실패 CAUTION이 결과가 와도 ACTIVE, 확인하면 사라짐 | `hmi/e2e/smoke.mjs` 1·2·2b·2d단계 |

### 3.23 REQ-HMI-004 경고 발생 조건과 차량 LOST 경고 2.0s (HMI)

**범위:** 표 3-1 중 탐지 노드 링크 LOST·DEGRADED와 신규 표적 탐지는 DET 연결(12월 3주)에서 TC를 추가한다(SDD 8.7). RTM에서 이 부분은 미검증으로 보인다.

**2.0s 측정 구간과 방법:**
- 구간: GW가 차량의 마지막 유효 패킷을 받은 시각 → HMI 화면에 차량 LOST 경고가 ACTIVE로 들어간 시각. 시작 시각은 GW가 보낸 VEHICLE `link_status` LOST의 `last_rx_ts`(GW 시계, ICD 7.1)이고, 끝 시각은 페이지 안 `MutationObserver`가 `data-kind="VEHICLE_LOST"`·`data-state="ACTIVE"` 요소를 처음 본 시각(시계 차를 옮기지 않은 페이지 시계)이다. GW와 브라우저가 같은 PC라 두 시계가 같다.
- 구성: GW LOST 판정(1.0s + 판정 주기 50ms 이하, TC-COM-003) + IF-03 전달 + HMI 반영. HMI 반영은 그 `link_status`로 낸 화면에 이미 경고가 있음을 U(TC-HMI-004-04)로 보인다(tick을 기다리지 않음).
- 방법: 시연 점검 3단계에서 SIM을 멈추고 위 두 시각의 차를 출력한다. 4장의 세 번(시계 차 0, +3s, −5s) 모두 0ms 초과 2000ms 이하이면 합격이다. 값은 STR에 적는다.
- 한계: 끝 시각은 DOM 반영 시각이라 그 뒤 화면 그리기(다음 프레임, 보통 16ms 이하)는 들어가지 않는다. GW와 HMI가 다른 PC인 배치는 두 시계가 달라 이 방법으로 재지 않는다.

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-HMI-004-01 | U | 조건별 입력: 차량 링크 LOST·DEGRADED, 모드 FAULT, FAILSAFE_ACTIVE, BATTERY_LOW, GW 재시도 대기·재시도 접속, 기준선 재설정 접속, STALE, 명령 TIMEOUT·REJECTED_STATE·ACCEPTED·결과 대기·결과 미확인 | 차량 LOST·FAULT·Failsafe·GW 끊김은 WARNING, GW 지연·배터리·명령 실패·결과 미확인은 CAUTION, DEGRADED는 ADVISORY. 수락과 결과 대기는 경고 없음, 명령 결과 경고는 명령마다 하나(보낸 순서로 번호) | `alerts.test.ts > REQ_HMI_004 each table 3-1 condition in scope raises an alert of its level` |
| TC-HMI-004-02 | U | link_status·텔레메트리를 받기 전, 첫 접속 중·열린 직후 | 경고 없음 (받지 않은 값과 첫 접속은 조건이 아님) | `alerts.test.ts > REQ_HMI_004 no alert comes from values never received or from the first connection attempt` |
| TC-HMI-004-03 | U | 차량 LOST·Failsafe·배터리 부족을 받은 뒤 GW 끊김 / HmiController에서 LOST 뒤 close 1006 → 재시도 → 재접속·CONNECTED | 차량 경고는 마지막으로 받은 값으로 유지되고 GW 끊김 WARNING이 더해짐 / 재시도 접속 중도 끊김, 재접속 뒤 둘 다 해제됨 | `alerts.test.ts > REQ_HMI_004 vehicle conditions keep the last received values while the GW is down`, `hmiController.test.ts > REQ_HMI_004 a GW disconnect raises a warning and the vehicle alerts stay from the last received values` |
| TC-HMI-004-04 | U | HmiController에 VEHICLE link_status LOST 하나 | 그 메시지로 `onChange`에 낸 화면에 차량 LOST WARNING ACTIVE (HMI 쪽 추가 지연 0) | `hmiController.test.ts > REQ_HMI_004 the vehicle LOST warning is in the view emitted for the link_status LOST itself` |
| TC-HMI-004-05 | U | FRESH 연결에서 GW 메시지가 끊김(가짜 시계, 250ms tick) | 마지막 stats 수신 후 2000ms 초과 2250ms 이하에 GW 지연 CAUTION | `hmiController.test.ts > REQ_HMI_004 the GW delay caution appears within one 250ms tick after the data turns stale` |
| TC-HMI-004-06 | U | HmiController에서 RTB 송신 → TIMEOUT, STOP 송신 → 결과 전 끊김 | 명령 실패 CAUTION, 이어서 결과 미확인 CAUTION(문구에 명령 이름) | `hmiController.test.ts > REQ_HMI_004 a command TIMEOUT and an unconfirmed command each raise a caution` |
| TC-HMI-004-07 | D | 실제 GW·SIM·HMI: SIM 중지(위 측정 방법), GW 중지, GW 재시작 | 마지막 유효 패킷 수신 → LOST 경고 표시 0ms 초과 2000ms 이하(값 출력), GW 중지 시 GW 끊김 WARNING과 차량 LOST WARNING ACTIVE, 재시작 뒤 GW 끊김은 해제됨 | `hmi/e2e/smoke.mjs` 3·4·5단계 |

### 3.24 REQ-LOG-001 유효 패킷·이벤트 DB 저장 (GW)

경고 원인 중 GW가 아는 것(링크 전이, 명령 송신·재전송·결과, TELEMETRY mode·status_flags)은 아래 기록에 들어 있다(SRS REQ-LOG-001). 통합 시험 환경은 2.8.

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-LOG-001-01 | U | TELEMETRY 행 (헤더 timestamp u64 최댓값, 음수 lat) | ICD 정수 값 그대로 10열(rx_ts, seq, tx_ts, lat, lon, heading, speed, battery, mode, status_flags), u64 최댓값 그대로 | `DbRow.REQ_LOG_001_telemetry_row_keeps_raw_icd_values` |
| TC-LOG-001-02 | U | DETECTION 행 (물체 2개, frame_id 40억) | 머리 값 그대로, objects jsonb에 물체 전부 | `DbRow.REQ_LOG_001_detection_row_keeps_every_object` |
| TC-LOG-001-03 | U | 링크 전이 (DETECTOR DEGRADED→LOST, 첫 패킷 전이) | event kind `link`, source DETECTOR, detail from·to·last_rx_ts(첫 전이는 null) | `DbRow.REQ_LOG_001_link_transition_is_an_event_with_source` |
| TC-LOG-001-04 | U | 명령 사건 4종 (WAYPOINT·STOP 송신, 형식 오류 결과, 무시, 모르는 ACK) | kind `cmd_sent`·`cmd_result`·`cmd_ignored`·`cmd_unknown_ack`, source NULL, WAYPOINT만 lat·lon | `DbRow.REQ_LOG_001_every_command_event_kind_is_stored` |
| TC-LOG-001-05 | U | NUL 문자가 든 req_id | detail에 `\u0000`이 없고 U+FFFD로 바뀜 (PostgreSQL이 NUL을 거부해 그 행이 늘 저장에 실패하지 않게) | `DbRow.REQ_LOG_001_nul_in_req_id_is_replaced_before_storing` |
| TC-LOG-001-06 | U | 세 테이블 행의 값 개수 | INSERT 문의 매개변수 수와 같음 | `DbRow.REQ_LOG_001_row_width_matches_insert_statement` |
| TC-LOG-001-07 | I | 실제 PostgreSQL. 유효 TELEMETRY 20개, CRC 오류 1개(seq 20), 유효 TELEMETRY 1개(seq 21), DETECTION(물체 2개) 1개 | seq 21이 저장된 뒤 다시 센 telemetry가 seq 0~19와 21(CRC 오류는 저장 안 됨), 값이 보낸 값과 같고 rx_ts가 IF-03 `rx_ts`와 같음. detection 1행, 물체 2개 | `test_REQ_LOG_001_valid_telemetry_and_detection_are_stored` |
| TC-LOG-001-08 | I | 실제 PostgreSQL. TELEMETRY 3개 뒤 중단, 받는 SIM 없이 STOP 명령(req_id에 NUL) | event에 VEHICLE 링크 LOST→CONNECTED→DEGRADED→LOST, `cmd_sent` attempt 1~4(같은 cmd_id), `cmd_result` TIMEOUT attempts 4, req_id는 U+FFFD로. 버림 0 | `test_REQ_LOG_001_link_transitions_and_command_events_are_stored` |
| TC-LOG-001-09 | U | `DbSink`, 가짜 세션 정상, 50건 | 넣은 순서대로 모두 저장, 버림 0 | `DbSink.REQ_LOG_001_writes_records_in_order_when_db_is_up` |

### 3.25 REQ-LOG-003 DB 저장이 중계를 막지 않음, 버림 계수, 재접속 (GW)

| TC ID | 수준 | 내용 | 기대 결과 | 테스트 |
|---|---|---|---|---|
| TC-LOG-003-01 | U | 재접속 시각, 가짜 시계 | 첫 시도는 바로. 실패 뒤 499ms는 아직, 500ms에 다시 시도 | `ReconnectSchedule.REQ_LOG_003_first_attempt_is_immediate_and_retry_waits_500ms` |
| TC-LOG-003-02 | U | 접속 실패 100회 | 간격 0.5·1·2·4·5·5s…, 100회째도 5s 뒤 다시 시도 (포기하지 않음) | `ReconnectSchedule.REQ_LOG_003_retry_interval_doubles_up_to_5s_and_never_stops` |
| TC-LOG-003-03 | U | 실패 2회 뒤 성공, 다시 실패 | 성공하면 처음 간격(0.5s)으로 | `ReconnectSchedule.REQ_LOG_003_success_resets_retry_interval` |
| TC-LOG-003-05 | U | 저장 호출이 멈춘 동안 1만 건 publish (상한 100) | DB가 멈춘 채로 publish가 모두 끝남(막히지 않음), queue_full = 1만 − 100. 풀리면 101건 저장, 저장 + 버림 = 넣은 수 | `DbSink.REQ_LOG_003_publish_does_not_block_while_db_write_hangs` |
| TC-LOG-003-06 | U | DB가 없는 동안 30건, 이후 DB 복구 | db_down 30, 재접속 5회 이상인데 알림은 ConnectFailed 1회(같은 사유), 복구 뒤 Connected와 저장 | `DbSink.REQ_LOG_003_records_while_db_is_down_are_dropped_and_reconnect_continues` |
| TC-LOG-003-07 | U | 한 번 저장한 연결에서 저장 중 테이블이 지워짐(다시 접속할 때까지 실패), 대기열 3건, 재접속 간격 1시간 | 그 행만 write_error 1, 대기열 3건은 버리지 않고 다시 접속(간격을 기다리지 않음) 뒤 순서대로 저장, db_down·queue_full 0, 알림 WriteFailed(다음 시도 0ms) → Connected | `DbSink.REQ_LOG_003_write_failure_drops_only_that_record_and_reconnects_at_once` |
| TC-LOG-003-08 | U | 저장 호출이 멈춘 채 `close()`, 대기열 2건, 그 뒤 publish, `close()` 다시, 멈춤을 풂 | DB가 멈춘 채로 close()가 끝남(GW 종료가 막히지 않음), 저장 중이던 행 + 대기열 2 = db_down 3, 그 뒤 기록 db_down 4. 두 번째 close()는 바로 돌아옴. 떼어 낸 스레드가 행을 마저 저장해도 다시 세지 않음(저장 + 버림 = 넣은 수 4) | `DbSink.REQ_LOG_003_close_does_not_wait_for_a_hung_db_and_counts_what_it_leaves` |
| TC-LOG-003-09 | I | `--db` 없이 기동, TELEMETRY 5개 | 중계 5개, 진단 `db log: disabled (no --db)` 외 DB 진단 없음 | `test_REQ_LOG_003_gateway_relays_without_db_option` |
| TC-LOG-003-10 | I | 없는 DB(소켓 없는 디렉터리)로 기동, 10Hz TELEMETRY 30개 | 기동·중계 30개, HEARTBEAT 간격 ≤ 1.1s, `db unavailable` 1회, db_down ≥ 31(TELEMETRY + 링크 전이), 다른 원인 0 | `test_REQ_LOG_003_unreachable_db_does_not_stop_relay_and_drops_are_counted` |
| TC-LOG-003-11 | I | 실제 PostgreSQL. 10Hz TELEMETRY 90개 중 1s에 DB 중지, 2.5s 뒤 재시작, 두 번째 `db connected` 뒤 TELEMETRY 5개 | 중계 90개 순서대로, HEARTBEAT 간격 ≤ 1.1s, `db write failed` 또는 `db unavailable`, 중지 전(seq < 10) 저장, 90개 중 일부만 저장, 재접속 뒤 보낸 5개 모두 저장. 최종 누계의 db_down + write_error ≥ 잃은 TELEMETRY 수, queue_full 0 | `test_REQ_LOG_003_db_restart_keeps_relay_and_reconnects` |
| TC-LOG-003-12 | I | 실제 PostgreSQL. GW를 맡은 서버 프로세스 SIGSTOP 동안 10Hz TELEMETRY 30개 | 중계 30개가 3.6s 안에, HEARTBEAT 간격 ≤ 1.1s. 풀면 31행 모두 저장, 최종 누계 버림 0 | `test_REQ_LOG_003_hung_db_does_not_delay_relay` |
| TC-LOG-003-13 | U | DB 없음(사유 "connection refused") 뒤 사유가 "password authentication failed"로 바뀜 | ConnectFailed를 사유마다 한 번씩, 바뀐 사유로 다시 알림 | `DbSink.REQ_LOG_003_changed_connect_failure_reason_is_reported_again` |
| TC-LOG-003-14 | U | 접속한 뒤 첫 저장이 실패, 재접속 간격 1시간, 이어서 150건 | 알림 WriteFailed의 다음 시도가 1시간(간격 적용), write_error 1, 기다리는 동안 들어온 기록은 대기열에 쌓이고 상한 100을 넘은 50건만 queue_full, db_down 0 | `DbSink.REQ_LOG_003_failure_before_any_write_on_a_connection_backs_off_and_keeps_queueing` |
| TC-LOG-003-15 | I | 실제 PostgreSQL, 서버 인코딩 EUC_KR. 10Hz TELEMETRY 30개 중에 STOP 명령 두 개(req_id "한글명령", 이모지가 든 req_id) | TELEMETRY 30개 모두 저장, 한글 req_id 명령의 이벤트는 저장되고 이모지 명령의 이벤트만 write_error(≥ 2), `db write failed, dropped 1 record` 알림, 최종 누계 db_down·queue_full 0 | `test_REQ_LOG_003_failed_record_does_not_cost_other_records` |
| TC-LOG-003-16 | I | 실제 PostgreSQL. 10Hz TELEMETRY 50개 중 1.5s에 다른 세션이 telemetry 테이블을 잠그고 지움(지우기 전 행 수를 남김) | 중계 50개, 다시 만든 테이블에 그 뒤 seq가 49까지 빠짐없이 저장, 최종 누계 db_down·queue_full 0, write_error ≤ 2, 지우기 전 행 + write_error + 다시 만든 뒤 행 = 50 | `test_REQ_LOG_003_dropped_table_is_recreated_by_reconnecting` |
| TC-LOG-003-17 | U | DB 스레드에서 예외(가짜 세션이 `std::bad_alloc`) | GW는 계속, 알림 Stopped, 저장 중이던 행과 그 뒤 기록은 db_down | `DbSink.REQ_LOG_003_exception_in_db_thread_stops_saving_without_crashing` |

- 막히지 않음의 판정: TC-LOG-003-12에서 `DbSink::publish`를 저장 완료까지 기다리게 바꾸면(동기 저장 변이) 중계가 2개에서 멈춰 실패함을 확인했다.
- TC-LOG-003-15는 접속 기본값에서 `client_encoding=UTF8`을 빼면 한글 req_id 이벤트가 저장되지 않아 실패한다. TC-LOG-003-16은 저장 실패 뒤 다시 접속하지 않게 바꾸면(2차 리뷰에서 찾은 결함) 실패한다(변이로 확인).

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

### TC-HMI-001-03, TC-HMI-002-03, TC-HMI-003-08, TC-HMI-004-07, TC-HMI-006-01, TC-HMI-006-11, TC-CMD-001-08·09, TC-CMD-004-02 시연 점검

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

3. 스크립트가 GW(빈 포트), vite, chromium을 띄우고 8단계(GW 접속 → SIM 송신 → 시계 +3초 계단 변화 → 명령 확인·송신 → 결과 전 GW 재시작과 경고가 붙은 새 명령 → SIM 중지 → GW 중지 → GW 재시작)를 화면 속성(`data-level`, `data-stale`, `data-heading`, 경고의 `data-kind`·`data-state`), 지도 레이어 점 수(`__ugvcTrackPoints`), 정확한 문구, 배너 변화 기록, GW WebSocket 생성 횟수, GW 진단 기록의 `cmd sent` 줄로 확인한다. 3단계는 차량 LOST 경고 지연(3.23 측정 방법)을 출력한다. 페이지 오류가 하나라도 나면 실패한다.
4. 합격: 세 번 모두 마지막 줄이 `PASS`이고, 실행 뒤 `ugvc_gateway`·`vite`·`ugvc_sim`(`python3 -m ugvc_sim`) 프로세스가 남지 않는다. 화면 캡처는 STR(05)에 붙인다.

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
