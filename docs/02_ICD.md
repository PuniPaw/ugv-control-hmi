# 인터페이스 정의서 (ICD)

| 항목 | 내용 |
|---|---|
| 문서번호 | UGVC-ICD-001 |
| 시스템 | AI 표적 탐지 기반 UGV 원격 관제 시스템 (가칭 UGVC) |
| 버전 | v0.5 |
| 작성일 | 2026-11-10 |
| 관련 문서 | UGVC-SRS-001, ADR-001 (영상 경로), ADR-002 (수치 표현·바이트 순서) |

## 개정 이력

| 버전 | 날짜 | 변경 내용 |
|---|---|---|
| v0.1 | 2026-11-10 | 최초 작성 |
| v0.2 | 2026-11-11 | 3장·9장 seq 판정을 스트림(송신원, msg_type) 단위로 명시, RFC 1982 경계(정확히 2^31은 "크지 않음"), 중복 seq는 order, 늦게 도착한 패킷은 유실 차감 없음, LOST 후 rebase는 그 송신원의 모든 스트림. 6.4 SIM의 범위 밖 COMMAND 처리(ACK REJECTED_INVALID). 7.1 `stats`: `lost_count`(유실 누계) 추가, `loss_rate_10s`는 창이 비면 `null`. `link_status.last_rx_ts`는 첫 패킷 전 `null`. 9장 규칙 1에 최대 길이 1200 명시. 7.1 공학 단위 값은 정수 LSB를 10의 거듭제곱으로 나눈 가장 가까운 십진수 |
| v0.3 | 2026-11-11 | 7.1 공학 단위 값의 정의를 JSON 숫자 텍스트가 아니라 값으로 명확화: 수신 측이 JSON 숫자를 IEEE 754 double로 읽으면 "LSB ÷ 10^k"의 정확한 십진수에 가장 가까운 double이 된다. 텍스트 표기(자릿수)는 정하지 않는다 (예: `96.0388503`와 `96.03885029999999`는 같은 double) |
| v0.5 | 2026-11-12 | 7장 HMI 의무 정밀화 (SRS REQ-HMI-006, PR #19 리뷰): 신선도의 "늦게 도착"은 연결별 기준선(관측한 수신 시각 − `ts`의 최솟값) 대비로 판정해 GW·HMI 시계 차와 무관하게 한다. 끊김 판정에 접속 시도 5초 제한·열린 연결 4초 무수신 추가. GW 시각 필드를 HMI 시각으로 바꿀 때도 같은 기준선을 쓴다 |
| v0.4 | 2026-11-11 | 7장 IF-03 연결 규약: 경로 외 요청 404(쿼리 문자열은 무시), 동시 접속 8개(넘으면 503, 핸드셰이크 중인 연결 포함), 접속 직후 현재 상태 송신, ping 응답 기한 2.0s(무응답 약 4s에 종료, 보내기만 하는 HMI는 판정 밖 → HMI가 ts로 신선도 감시, TBD-04), 업그레이드 요청 헤더 4KB·본문 불허·헤더 수신 중 연결 16개, 종료 사유 기록, HMI별 송신 대기열 1000개(넘치면 연결 종료), 끊긴 뒤 HMI 재접속 규약(1006, 1~5s 백오프), 7.2 명령은 명령 중계 구현 전까지 무시 |

---

## 1. 개요

이 문서는 UGVC 구성요소 사이의 메시지 형식, 전송 규약, 검증 규칙을 정의한다. 헤더의 `version` 필드는 이 문서의 주 버전과 일치시킨다 (v0.x → `0x01`).

## 2. 인터페이스 목록

| ID | 송신 → 수신 | 프로토콜 | 포트 | 형식 | 메시지 |
|---|---|---|---|---|---|
| IF-01a | SIM → GW | UDP | GW 15001 | 바이너리 | TELEMETRY, ACK |
| IF-01b | GW → SIM | UDP | SIM 15010 | 바이너리 | COMMAND, HEARTBEAT |
| IF-02 | DET → GW | UDP | GW 15002 | 바이너리 | DETECTION |
| IF-03 | GW ↔ HMI | WebSocket | GW 8080 `/ws` | JSON | 7장 참조 |
| IF-04 | DET → HMI | WebSocket | DET 8081 `/video` | 바이너리 + JPEG | 8장 참조 |

## 3. 공통 규약 (IF-01, IF-02)

| 항목 | 규약 |
|---|---|
| 바이트 순서 | Big-endian (네트워크 바이트 순서) |
| 수치 표현 | 정수형 고정소수점. 필드마다 LSB(최소 단위)를 명시한다. 부동소수점은 사용하지 않는다. |
| 직렬화 | 필드별 명시적 직렬화. 구조체 memcpy 금지 (ADR-002) |
| 시각 | Unix epoch 기준 밀리초, UTC, `u64` |
| 최대 패킷 크기 | 1200 바이트 (IP 단편화 회피) |
| 무결성 | CRC-16/CCITT-FALSE (다항식 0x1021, 초기값 0xFFFF, 반사 없음, 최종 XOR 0x0000) |
| CRC 범위 | 헤더 시작부터 페이로드 끝까지 (CRC 필드 제외) |
| seq | 스트림(송신원·msg_type)별로 0부터 1씩 증가, `u32` 최대값 다음은 0 |
| 예약 비트 | 0으로 송신, 수신 측은 무시 |

## 4. 공통 헤더 (18 바이트) + CRC (2 바이트)

| 오프셋 | 필드 | 타입 | 값 / 설명 |
|---|---|---|---|
| 0 | sync | u16 | `0xAA55` 고정 |
| 2 | version | u8 | `0x01` |
| 3 | msg_type | u8 | 5장 참조 |
| 4 | seq | u32 | 일련번호 |
| 8 | timestamp | u64 | 송신 시각 (ms) |
| 16 | payload_len | u16 | 페이로드 길이 N (바이트) |
| 18 | payload | N 바이트 | 메시지별 본문 |
| 18 + N | crc16 | u16 | CRC-16/CCITT-FALSE |

전체 패킷 길이 = 20 + N

## 5. 메시지 목록

| msg_type | 이름 | 인터페이스 | 주기 | 페이로드 길이 |
|---|---|---|---|---|
| `0x01` | TELEMETRY | IF-01a | 10Hz | 16 |
| `0x02` | DETECTION | IF-02 | 프레임마다 (목표 10Hz) | 17 + 11 × count |
| `0x10` | COMMAND | IF-01b | 이벤트 (재전송 포함) | 13 |
| `0x11` | ACK | IF-01a | COMMAND 수신 시 | 5 |
| `0x20` | HEARTBEAT | IF-01b | 1Hz | 0 |

## 6. 메시지 상세

오프셋은 페이로드 시작 기준이다.

### 6.1 TELEMETRY (`0x01`, 16 바이트)

| 오프셋 | 필드 | 타입 | LSB / 단위 | 범위 | 설명 |
|---|---|---|---|---|---|
| 0 | lat | i32 | 1e-7 deg | -900000000 ~ 900000000 | 위도 (WGS84) |
| 4 | lon | i32 | 1e-7 deg | -1800000000 ~ 1800000000 | 경도 (WGS84) |
| 8 | heading | u16 | 0.01 deg | 0 ~ 35999 | 진북 기준 시계방향 |
| 10 | speed | u16 | 0.01 m/s | 0 ~ 3000 | 대지 속도 |
| 12 | battery | u8 | 1 % | 0 ~ 100 | 잔량 |
| 13 | mode | u8 | enum | 0 ~ 4 | 표 6-1 |
| 14 | status_flags | u16 | bitfield | — | 표 6-2 |

**표 6-1. mode**

| 값 | 이름 | 설명 |
|---|---|---|
| 0 | STANDBY | 대기 |
| 1 | MISSION | 경로점 주행 중 |
| 2 | STOPPED | 정지 (명령 또는 Failsafe) |
| 3 | RTB | 복귀 중 |
| 4 | FAULT | 고장 |

**표 6-2. status_flags**

| 비트 | 이름 | 설명 |
|---|---|---|
| 0 | GPS_VALID | 위치 유효 |
| 1 | CAMERA_OK | 카메라 정상 |
| 2 | BATTERY_LOW | 배터리 20% 이하 |
| 3 | FAILSAFE_ACTIVE | HEARTBEAT 두절로 자율 정지함 (SRS REQ-SAFE-001) |
| 4–15 | 예약 | 0 |

### 6.2 DETECTION (`0x02`, 17 + 11 × count 바이트)

| 오프셋 | 필드 | 타입 | LSB / 단위 | 범위 | 설명 |
|---|---|---|---|---|---|
| 0 | frame_id | u32 | — | — | IF-04 프레임과 매칭하는 키 |
| 4 | capture_ts | u64 | ms | — | 프레임 캡처 시각 (지연 측정용) |
| 12 | img_w | u16 | px | 1 ~ 4096 | 원본 프레임 너비 |
| 14 | img_h | u16 | px | 1 ~ 4096 | 원본 프레임 높이 |
| 16 | count | u8 | — | 0 ~ 32 | 객체 수 (0이면 "탐지 없음"을 뜻함) |
| 17 | objects | 11 × count | — | — | 아래 객체 구조 반복 |

**객체 구조 (11 바이트)**

| 상대 오프셋 | 필드 | 타입 | LSB / 단위 | 범위 | 설명 |
|---|---|---|---|---|---|
| +0 | class_id | u8 | enum | 표 6-3 | 객체 종류 |
| +1 | confidence | u16 | 0.0001 | 0 ~ 10000 | 신뢰도 |
| +3 | x | u16 | px | 0 ~ img_w-1 | 박스 좌상단 x |
| +5 | y | u16 | px | 0 ~ img_h-1 | 박스 좌상단 y |
| +7 | w | u16 | px | 1 ~ img_w - x | 박스 너비 |
| +9 | h | u16 | px | 1 ~ img_h - y | 박스 높이 |

최대 길이: 17 + 11 × 32 = 369 바이트

**표 6-3. class_id (TBD-02, 부트캠프 모델 확정 후 갱신)**

| 값 | 이름 |
|---|---|
| 0 | PERSON |
| 1 | VEHICLE |
| 2 | ARMORED_VEHICLE |
| 255 | UNKNOWN |

### 6.3 COMMAND (`0x10`, 13 바이트)

| 오프셋 | 필드 | 타입 | LSB / 단위 | 범위 | 설명 |
|---|---|---|---|---|---|
| 0 | cmd_id | u32 | — | 1 이상 | GW가 발급. 재전송 시 같은 값 유지 |
| 4 | cmd_type | u8 | enum | 표 6-4 | 명령 종류 |
| 5 | lat | i32 | 1e-7 deg | TELEMETRY와 동일 | WAYPOINT만 유효, 그 외 0 |
| 9 | lon | i32 | 1e-7 deg | TELEMETRY와 동일 | WAYPOINT만 유효, 그 외 0 |

**표 6-4. cmd_type**

| 값 | 이름 | 설명 |
|---|---|---|
| 0x01 | STOP | 즉시 정지 |
| 0x02 | RTB | 출발 지점으로 복귀 |
| 0x03 | WAYPOINT | 경로점 추가 후 MISSION 진입 |

SIM은 이미 처리한 cmd_id를 다시 받으면 재실행하지 않고 이전 result로 ACK만 다시 보낸다 (SRS REQ-CMD-003).

### 6.4 ACK (`0x11`, 5 바이트)

| 오프셋 | 필드 | 타입 | 범위 | 설명 |
|---|---|---|---|---|
| 0 | cmd_id | u32 | — | 응답 대상 명령 |
| 4 | result | u8 | 표 6-5 | 처리 결과 |

**표 6-5. result**

| 값 | 이름 | 설명 |
|---|---|---|
| 0 | ACCEPTED | 수락 |
| 1 | REJECTED_INVALID | 필드 값 오류 (범위 밖 좌표 등). SIM은 cmd_id ≥ 1이고 cmd_type·좌표가 6.3 범위를 벗어난 COMMAND를 실행하지 않고 이 값으로 ACK한다. cmd_id가 0이면 응답할 대상이 없으므로 ACK 없이 폐기한다 |
| 2 | REJECTED_STATE | 현재 상태에서 수행 불가 (FAULT 등) |

### 6.5 HEARTBEAT (`0x20`, 0 바이트)

페이로드 없음. SIM은 3.0초 이상 수신하지 못하면 Failsafe 정지한다 (SRS REQ-SAFE-001).

---

## 7. IF-03 GW ↔ HMI (WebSocket JSON)

- 업그레이드 요청은 헤더 4KB까지, 본문 없이 보낸다 (본문이 있거나 헤더가 크면 응답 없이 닫는다). 요청 헤더를 아직 다 보내지 않은 연결은 16개까지이며, 넘는 연결은 바로 닫는다.
- 주소: `ws://<GW>:8080/ws`. 경로만 비교하므로 `/ws?client=hmi`처럼 쿼리를 붙여도 된다. 다른 경로의 요청에는 HTTP 404로 답하고 연결을 닫는다. 동시 접속은 HMI 8개까지이며, 넘는 요청에는 HTTP 503으로 답한다. GW는 기본으로 127.0.0.1에만 열며, 다른 PC의 HMI는 GW를 `--ws-host 0.0.0.0`으로 실행해 받는다.
- 접속 직후 GW는 송신원별 현재 `stats`와 `link_status`를 먼저 보낸다. HMI는 다음 1Hz 보고를 기다리지 않고 링크 상태를 그릴 수 있다.
- 모든 HMI는 같은 메시지를 같은 순서로 받는다. 연결된 HMI가 받는 메시지 열에는 빈틈이 없다.
- **ping 응답 기한 2.0s.** GW는 2초마다 판정해, 직전 2초 동안 HMI에게서 받은 것이 없으면 WebSocket ping을 보내고, 다음 판정(2초 뒤)까지도 받은 것이 없으면 연결을 끊는다. 따라서 아무것도 보내지 않는 HMI는 무응답 약 4초에 끊기고, ping에 2초 안에 답하는 HMI는 유지된다. 소켓을 읽지 않고 **아무것도 보내지 않는** HMI는 쌓인 데이터 뒤의 ping을 읽지 못해 pong을 보내지 못하므로, TCP 버퍼 크기와 무관하게 끊긴다 (다른 HMI와 GW 판정에는 영향 없음, REQ-COM-008). WebSocket 구현(브라우저, Python `websockets` 등)은 ping에 자동으로 응답하므로 HMI가 할 일은 **받은 메시지를 계속 소비하는 것**이다.
- **이 판정이 보장하지 않는 것.** GW는 HMI에게서 받은 바이트가 무엇이든(명령 텍스트 포함) 응답으로 센다. 그래서 명령을 보내면서 수신은 밀린 HMI, 또는 네트워크 계층은 pong을 보내지만 화면 처리(JS `onmessage`)가 밀린 HMI는 이 판정으로 끊기지 않는다. **데이터 신선도는 HMI가 직접 감시한다 (SRS REQ-HMI-006):**
  - HMI는 연결마다 **기준선** = 이 연결에서 받은 메시지의 (HMI 수신 시각 − `ts`) 최솟값을 둔다. 기준선에는 두 PC의 시계 차와 최소 전달 지연이 들어 있으므로, 이후 판정은 기준선과의 차이만 본다 (GW와 HMI가 다른 PC여도 시계를 맞출 필요 없음).
  - 마지막 `stats`를 받은 지 2초가 넘거나, 마지막 `stats`의 (수신 시각 − `ts`)가 기준선보다 2초 넘게 크면 "GW 데이터 지연"을 표시한다.
  - GW 시각 필드(`last_rx_ts` 등)를 HMI 화면의 경과 시간으로 바꿀 때도 `GW 시각 + 기준선`을 쓴다.
  - 한계: 접속 직후부터 이미 밀려 있던 HMI는 그 지연이 기준선에 들어가 탐지되지 않는다. 연결 중 어느 한쪽 시계가 계단식으로 바뀌면 재접속까지 판정이 그만큼 어긋난다.
  - 정식 경고(REQ-HMI-004)는 이 판정을 입력으로 쓴다. GW 쪽 보장(앱 수준 확인 응답 등)은 TBD-04.
- **송신 대기열 1000개.** ping 기한보다 짧은 시간에 메시지가 몰려 HMI별 대기열이 넘쳐도 연결을 끊는다 (메모리 상한).
- GW는 연결을 끊을 때마다 사유(`lag limit`, `queue full`, `closed by HMI` 등)를 진단(stderr)에 한 줄로 기록한다.
- **끊긴 뒤.** GW가 끊을 때는 WebSocket close 프레임 없이 TCP 연결을 닫는다 (뒤처진 HMI에는 close 프레임도 제때 닿지 않는다). HMI는 close code 1006(비정상 종료)으로 이를 받고, "GW 연결 끊김"을 표시한 뒤 1초부터 시작해 두 배씩 늘려 최대 5초 간격으로 재접속한다. 재접속하면 현재 `stats`·`link_status`부터 받는다. GW PC의 전원·네트워크가 끊겨 FIN/RST조차 오지 않는 경우를 위해, HMI는 접속 시도가 5초 안에 열리지 않거나 열린 연결에서 4초 동안 아무것도 받지 못하면(정상이면 송신원별 1Hz `stats`가 온다) 스스로 닫고 같은 간격으로 재접속한다 (SRS REQ-HMI-006).
- HMI가 보내는 메시지 한 개는 64KB를 넘지 않는다.
- GW는 바이너리 필드를 공학 단위(deg, m/s, 0~1 신뢰도)로 변환해서 전달한다.
- 공학 단위 값은 **값**으로 정의한다: JSON 숫자를 IEEE 754 double로 읽으면 "정수 LSB ÷ 10^k"(예: lat LSB 960388503 → 96.0388503)의 정확한 십진수에 가장 가까운 double이 된다. JSON 텍스트의 자릿수는 정하지 않는다. 수신 측은 텍스트를 비교하지 말고 값으로 다룬다 (`JSON.parse` 결과를 그대로 쓰면 된다).
- 모든 메시지는 공통 봉투를 쓴다:

```json
{ "type": "telemetry", "ts": 1794729600000, "data": { } }
```

### 7.1 GW → HMI

| type | 주기 | data 필드 |
|---|---|---|
| `telemetry` | 수신 즉시 | lat, lon, heading_deg, speed_mps, battery_pct, mode, flags[], seq, rx_ts |
| `detection` | 수신 즉시 | frame_id, capture_ts, img_w, img_h, objects[{class, conf, x, y, w, h}] |
| `link_status` | 상태 전이 시 + 1Hz | source(`VEHICLE`/`DETECTOR`), state(`CONNECTED`/`DEGRADED`/`LOST`, 첫 패킷 전은 `LOST`), last_rx_ts(첫 패킷 전 `null`) |
| `stats` | 1Hz | source, loss_rate_10s(0~1, 창이 비면 `null`), rx_count(유효 패킷 누계), lost_count(유실 누계), errors{sync, version, length, crc, type, range, order} |
| `cmd_result` | 결과 확정 시 | req_id, cmd_id, result(`ACCEPTED`/`REJECTED_INVALID`/`REJECTED_STATE`/`TIMEOUT`), attempts |

예시:

```json
{
  "type": "telemetry",
  "ts": 1794729600123,
  "data": {
    "lat": 36.1234567, "lon": 127.7654321,
    "heading_deg": 87.25, "speed_mps": 4.5,
    "battery_pct": 76, "mode": "MISSION",
    "flags": ["GPS_VALID", "CAMERA_OK"],
    "seq": 1532, "rx_ts": 1794729600118
  }
}
```

### 7.2 HMI → GW

| type | data 필드 | 설명 |
|---|---|---|
| `command` | req_id(문자열, HMI 발급), cmd_type(`STOP`/`RTB`/`WAYPOINT`), lat?, lon? | 운용자 확인을 거친 뒤에만 송신 (REQ-CMD-001) |

v0.4 시점의 GW는 7.2 메시지를 받아도 처리하지 않고 진단에 기록만 한다. 명령 송신·재전송·`cmd_result`는 12월 명령 중계 작업(REQ-CMD-002)에서 구현한다.

## 8. IF-04 DET → HMI 영상 (WebSocket 바이너리)

- 주소: `ws://<DET>:8081/video`
- 프레임 하나 = WebSocket 바이너리 메시지 하나
- 목표: 640 × 480, 10fps, JPEG

| 오프셋 | 필드 | 타입 | 설명 |
|---|---|---|---|
| 0 | frame_id | u32 (BE) | DETECTION.frame_id와 같은 값 |
| 4 | capture_ts | u64 (BE) | DETECTION.capture_ts와 같은 값 |
| 12 | jpeg | 나머지 전체 | JPEG 이미지 |

## 9. 수신 검증 규칙 (GW)

아래 순서로 검사하고, 하나라도 실패하면 즉시 폐기하고 해당 오류 계수를 1 증가시킨다.

| 순서 | 검사 | 오류 계수 |
|---|---|---|
| 1 | 20 ≤ 수신 길이 ≤ 1200 (3장 최대 패킷 크기) | length |
| 2 | sync == `0xAA55` | sync |
| 3 | version == `0x01` | version |
| 4 | 수신 길이 == 20 + payload_len | length |
| 5 | CRC 일치 | crc |
| 6 | msg_type이 해당 포트에서 허용된 값 | type |
| 7 | payload_len이 5장 정의와 일치 (DETECTION은 count로 계산) | length |
| 8 | 필드 값이 6장 범위 안 | range |
| 9 | seq가 같은 스트림의 직전 seq보다 큼 (u32 순환 고려). 같으면(중복) 실패 | order |

seq 처리:
- seq 비교와 유실 계산은 스트림(송신원, msg_type)마다 따로 한다. 예: VEHICLE의 TELEMETRY와 ACK는 서로 다른 스트림이다.
- 직전 seq + 1이 아니면 그 차이 − 1을 유실 패킷 수에 더한다. 나중에 늦게 도착한 패킷은 규칙 9로 폐기하고, 이미 더한 유실 수는 줄이지 않는다.
- 순환 비교는 RFC 1982 방식을 따른다: (새 seq − 직전 seq) mod 2^32가 1 이상 2^31 미만이면 "큼". 정확히 2^31이면 "크지 않음"으로 본다.
- 송신원 재시작 감지를 위해, 링크 상태가 LOST인 송신원에게서 받은 첫 유효 패킷은 seq 비교 없이 기준값으로 삼는다. 이때 그 송신원의 모든 스트림이 기준값을 다시 잡는다.

## 10. 미확정 사항

| ID | 내용 |
|---|---|
| TBD-02 | class_id 목록 (6.2) |
| TBD-03 | 다중 경로점 일괄 전송 메시지 필요 여부 (고도화 단계 검토) |
| TBD-04 | IF-03에서 HMI 화면 처리 지연을 GW가 감지할 방법 (예: HMI의 앱 수준 확인 응답). 지금은 HMI가 `ts`로 신선도를 직접 감시한다 (7장) |
