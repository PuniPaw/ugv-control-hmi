# UGVC — AI 표적 탐지 기반 UGV 원격 관제 시스템

방산 SW/AI 직무 취업용 포트폴리오 프로젝트. 1인 개발.
실시간 텔레메트리와 AI 탐지 결과를 통합해 운용자가 빠르게 판단하도록 돕는 무인체계 관제 HMI를 만든다.
코드만큼 **방산형 개발 프로세스(요구사항 → 설계 → 구현 → 시험 추적)**를 보여 주는 것이 목적이다.

- 마감: **2026-12-31 MVP 완성** (README, 시연 영상, 포트폴리오 PDF 포함). 1월부터 지원서에 사용.
- 개발자 배경: 프론트엔드(Vue/TS) 실무 경험 있음. **C++은 복습·학습 중**이므로 게이트웨이 코드는 설계 의도와 C++ 관용구를 짧게 설명하며 진행한다.

---

## 1. 시스템 구성

```
[SIM 차량 시뮬레이터, Python] ──UDP IF-01a (TELEMETRY, ACK)──▶ [GW 통신 게이트웨이, C++17]
                              ◀─UDP IF-01b (COMMAND, HEARTBEAT)─
[DET AI 탐지 노드, Python]   ──UDP IF-02 (DETECTION)─────────▶ [GW]
[GW] ◀──WebSocket JSON IF-03──▶ [HMI 관제 화면, Vue 3 + TS]
[DET] ──WebSocket 바이너리 IF-04 (JPEG 프레임)──▶ [HMI]
[GW] ──▶ [PostgreSQL 로그 DB]
```

| CSCI | 언어 | 역할 | 포트 |
|---|---|---|---|
| SIM | Python | 경로점 주행 모사, 10Hz 텔레메트리, 명령 수행, Failsafe | 수신 UDP 15010 |
| DET | Python | 영상 재생, 객체 탐지(부트캠프 모델), 탐지 결과·영상 송신 | WS 8081 `/video` |
| GW | C++17 | 패킷 검증, 링크 상태 판정, HMI 중계, 명령 재전송, DB 저장 | UDP 15001(SIM), 15002(DET), WS 8080 `/ws` |
| HMI | Vue 3 + TS | 지도(Leaflet), 영상+박스, 상태 패널, 경고, 명령, 사후 재생 | — |

### 기술 스택 (확정)
- GW: C++17, CMake, standalone Asio(UDP), Boost.Beast 또는 uWebSockets(WebSocket, 미정), GoogleTest, cppcheck, libpq 또는 libpqxx
- SIM/DET: Python 3, `struct.pack(">...")`로 직렬화
- HMI: Vue 3 + TypeScript, Leaflet (고도화 단계에서 오프라인 타일)
- 저장소: 모노레포 `sim/ detector/ gateway/ hmi/ docs/` + `python/`(ugvc_proto, SIM·DET 공용 codec), `testdata/vectors/`(C++·Python 공용 벡터), `tools/`
- 테스트 벡터는 `tools/vectors/gen_vectors.py`가 표준 라이브러리만으로 생성한다 (우리 codec을 import하지 않음 → 독립 기준)
- C++ 의존성(FetchContent, git): standalone Asio 1.30.2, nlohmann/json v3.11.3(IF-03 JSON·벡터 로드), GoogleTest v1.15.2
- 통합 시험: `tests/integration/` (pytest가 GW를 subprocess로 실행)

---

## 2. 핵심 규격 요약 (원본은 docs/02_ICD.md)

- **Big-endian, 정수 고정소수점, 필드별 명시 직렬화. 구조체 memcpy·`#pragma pack` 금지** (ADR-002)
- 헤더 18B + payload N + CRC16 2B = 20 + N 바이트, 최대 1200B
  - `sync u16 = 0xAA55 | version u8 = 0x01 | msg_type u8 | seq u32 | timestamp u64(ms) | payload_len u16`
- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, 반사 없음, xorout 0). 범위: 헤더 시작~payload 끝
  - 테스트 벡터: ASCII `"123456789"` → `0x29B1`
- 메시지: `0x01` TELEMETRY(16B), `0x02` DETECTION(17+11×count, count ≤ 32), `0x10` COMMAND(13B), `0x11` ACK(5B), `0x20` HEARTBEAT(0B)
- GW 수신 검증 순서 (ICD 9장): 길이≥20 → sync → version → 길이==20+N → CRC → 포트별 허용 type → type별 길이 → 필드 범위 → seq 순서(RFC 1982). 실패 시 즉시 폐기 + 원인별 계수
- 링크 상태: 마지막 유효 패킷 후 0.5s DEGRADED, 1.0s LOST. LOST 후 첫 유효 패킷은 seq 기준값 재설정
- 명령: ACK 1.0s 대기, 같은 cmd_id로 최대 3회 재전송 후 TIMEOUT. SIM은 중복 cmd_id 재실행 금지
- Failsafe: SIM은 HEARTBEAT 3.0s 미수신 시 STOPPED + FAILSAFE_ACTIVE, 링크 복구돼도 명령 없이 재개 금지
- 영상은 GW를 거치지 않음. HMI가 frame_id로 영상과 탐지 결과를 매칭, 0.5s 안에 결과 없으면 박스 없이 표시 (ADR-001)

---

## 3. 작업 규칙 (추적성 유지)

- **ID 체계**: `REQ-<영역>-NNN` (영역: COM, CMD, AI, HMI, LOG, PERF, SAFE, QUAL), 시험 `TC-<영역>-NNN-NN`, 결정 `ADR-NNN`, 미확정 `TBD-NN`
- **브랜치**: `main`이 기준. 작업마다 `main`에서 `feat/...`, `fix/...`, `docs/...` 브랜치를 만들어 작업한다
- **커밋 메시지**: Conventional Commits + 관련 요구사항 ID
  - 예: `feat(gateway): 통신 두절 감지 [REQ-COM-003]`
- **테스트 이름**: 요구사항 ID 포함 → 추적표(RTM)를 grep 스크립트로 자동 생성할 예정
  - 예: `TEST(LinkMonitor, REQ_COM_003_lost_after_1s)`
- **규격 변경 시**: ICD 표 → C++ 코드 → Python 코드를 함께 수정하고, 문서 맨 위 개정 이력에 기록. 결정이 바뀌면 ADR 추가
- **시간 의존 로직**(링크 판정, 재전송)은 시계를 주입 가능하게 만들어 단위 시험에서 가짜 시계로 검증
- 문서는 마크다운 + Mermaid. 문서 원본은 `docs/`에 두고 Git으로 형상관리

---

## 4. 문서 현황 (`docs/`)

| 문서 | 상태 | 비고 |
|---|---|---|
| 00_overview.md | 미작성 | 시스템 개요·용어. README와 함께 마지막에 정리 |
| 01_SRS.md (UGVC-SRS-001) | **v0.1 완료** | 요구사항 34개, MVP/고도화 구분 |
| 02_ICD.md (UGVC-ICD-001) | **v0.1 완료** | IF-01~04, 검증 규칙 |
| 03_SDD.md | 미작성 | 모듈 구조, 링크 상태도, 명령 재전송 시퀀스, 스레드 구조 (2~3쪽) |
| 04_STP.md (UGVC-STP-001) | **v0.1 완료** | TC-COM-001~007, TC-QUAL-001 (49개). CMD·SAFE 등은 구현 주에 추가 |
| 05_STR.md | 미작성 | 시험 실행 결과 |
| 06_RTM.md | 미작성 | 스크립트로 생성 예정 |
| adr/ADR-001 영상 전송 경로 분리 | **승인** | |
| adr/ADR-002 big-endian·고정소수점·명시 직렬화 | **승인** | |
| CHANGELOG.md | 미작성 | |

### SRS v0.2에서 고칠 것
1. REQ-COM-006: "작은 seq" → "RFC 1982 순환 비교상 이전인 seq" (ICD 9장과 일치시키기)
2. REQ-COM-005·006: seq 순서·유실은 "송신원별"이 아니라 **스트림(송신원, msg_type)별**로 판정 (ICD 3장과 일치). 링크 상태는 송신원별 그대로
3. REQ-COM-005: 유실률 = 유실 / (수신 + 유실), 1초 버킷 10개 슬라이딩 창, 창이 비면 `null` (ICD 7.1 `stats.loss_rate_10s`도 nullable로)
4. REQ-COM-003: 경계는 `≥` (정확히 0.5s에 DEGRADED, 1.0s에 LOST)
5. REQ-COM-006: 중복 seq도 역순 도착으로 보고 `order` 계수
6. 초기 링크 상태 LOST(시작 시 이벤트 없음), 늦게 도착한 패킷은 유실 차감 없음, RFC 1982 차이 정확히 2^31은 폐기, DETECTION count 초과는 range, HEARTBEAT는 링크 상태와 무관하게 송신 (STP 5장 표가 정리본)
7. REQ-HMI-004의 2.0s는 GW LOST 판정(1.0s) + WS 전달 + 화면 반영 합계. STP에서 측정 구간과 방법을 정할 것

### 미확정 사항
- TBD-01: 영상-탐지 종단 지연 목표값 (12월 3주 측정 후)
- TBD-02: class_id 목록 (부트캠프 모델 확정 시, 현재 PERSON/VEHICLE/ARMORED_VEHICLE/UNKNOWN)
- TBD-03: 다중 경로점 일괄 전송 메시지 (고도화)
- GW WebSocket 라이브러리: Boost.Beast vs uWebSockets

---

## 5. 일정과 현재 위치

| 기간 | 할 일 | 상태 |
|---|---|---|
| 11/4–11/9 | 범위 확정, SRS·ICD v0.1, ADR, 저장소 구성 | 문서 완료, **저장소 구성 확인 필요** |
| **11/10–11/16 (현재)** | SIM TELEMETRY 송신, C++ UDP 수신·헤더 파싱, CRC 단위 시험, STP(TC-COM) 초안 | 시작 전 |
| 11/17–11/23 | 검증 규칙 9단계 완성, 링크 상태 판정, seq 유실률, cmd 시뮬 수신부, cppcheck 적용 | |
| 11/24–11/30 | GW → HMI WebSocket 중계, HMI 뼈대(지도, 상태 패널) | |
| 12월 1–2주 | COMMAND/ACK 재전송, HEARTBEAT·Failsafe, 경고 체계, PostgreSQL 로그 | |
| 12월 3주 | DET 연결, 영상 오버레이, 지연 측정 → MVP 완성 | |
| 12월 4주 | README, 시연 영상, 포트폴리오 PDF, STR·RTM 정리 | |

## 6. 바로 다음 할 일

1. ✅ 모노레포 골격 생성: `gateway/`(CMake + GoogleTest FetchContent), `sim/`, `detector/`, `hmi/`, `docs/`, `.gitignore`, 루트 README 초안
2. ✅ `gateway/`: `codec`(big-endian read/write 헬퍼, CRC16, 헤더 encode/decode)부터 구현하고 단위 시험
   - CRC 벡터 `"123456789"` → `0x29B1`, 헤더 라운드트립, 검증 규칙별 실패 케이스
3. ✅ `sim/`: TELEMETRY 10Hz 송신 스크립트 (고정 경로점 직선 주행이면 충분)
4. ✅ GW에서 UDP 15001 수신 → 파싱 → 콘솔 출력까지 연결 (Python 송신과 C++ 수신의 바이트 일치 확인)
5. `docs/04_STP.md` 초안: REQ-COM-001~007, REQ-QUAL-001에 대한 TC 작성

---

## Agent skills

### Issue tracker

작업 티켓은 GitHub Issues(`PuniPaw/ugv-control-hmi`)에서 관리한다. See `docs/agents/issue-tracker.md`.

### Triage labels

기본 다섯 라벨(`needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`)을 그대로 쓴다. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: 루트 `GLOSSARY.md` + `docs/adr/`. See `docs/agents/domain.md`.
