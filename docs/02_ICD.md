# 인터페이스 정의서 (ICD)

| 항목 | 내용 |
|---|---|
| 문서번호 | UGVC-ICD-001 |
| 시스템 | AI 표적 탐지 기반 UGV 원격 관제 시스템 (가칭 UGVC) |
| 버전 | v0.1 (초안) |
| 작성일 | 2026-11-10 |
| 관련 문서 | UGVC-SRS-001, ADR-001 (영상 경로), ADR-002 (수치 표현·바이트 순서) |

## 개정 이력

| 버전 | 날짜 | 변경 내용 |
|---|---|---|
| v0.1 | 2026-11-10 | 최초 작성 |

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
| seq | 송신원·msg_type별로 0부터 1씩 증가, `u32` 최대값 다음은 0 |
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
| 1 | REJECTED_INVALID | 필드 값 오류 (범위 밖 좌표 등) |
| 2 | REJECTED_STATE | 현재 상태에서 수행 불가 (FAULT 등) |

### 6.5 HEARTBEAT (`0x20`, 0 바이트)

페이로드 없음. SIM은 3.0초 이상 수신하지 못하면 Failsafe 정지한다 (SRS REQ-SAFE-001).

---

## 7. IF-03 GW ↔ HMI (WebSocket JSON)

- 주소: `ws://<GW>:8080/ws`
- GW는 바이너리 필드를 공학 단위(deg, m/s, 0~1 신뢰도)로 변환해서 전달한다.
- 모든 메시지는 공통 봉투를 쓴다:

```json
{ "type": "telemetry", "ts": 1794729600000, "data": { } }
```

### 7.1 GW → HMI

| type | 주기 | data 필드 |
|---|---|---|
| `telemetry` | 수신 즉시 | lat, lon, heading_deg, speed_mps, battery_pct, mode, flags[], seq, rx_ts |
| `detection` | 수신 즉시 | frame_id, capture_ts, img_w, img_h, objects[{class, conf, x, y, w, h}] |
| `link_status` | 상태 전이 시 + 1Hz | source(`VEHICLE`/`DETECTOR`), state(`CONNECTED`/`DEGRADED`/`LOST`), last_rx_ts |
| `stats` | 1Hz | source, loss_rate_10s, rx_count, errors{sync, version, length, crc, type, range, order} |
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
| 1 | 수신 길이 ≥ 20 | length |
| 2 | sync == `0xAA55` | sync |
| 3 | version == `0x01` | version |
| 4 | 수신 길이 == 20 + payload_len | length |
| 5 | CRC 일치 | crc |
| 6 | msg_type이 해당 포트에서 허용된 값 | type |
| 7 | payload_len이 5장 정의와 일치 (DETECTION은 count로 계산) | length |
| 8 | 필드 값이 6장 범위 안 | range |
| 9 | seq가 직전 seq보다 큼 (u32 순환 고려) | order |

seq 처리:
- 직전 seq + 1이 아니면 그 차이 − 1을 유실 패킷 수에 더한다.
- 순환 비교는 RFC 1982 방식(차이가 2^31 미만이면 "큼")을 따른다.
- 송신원 재시작 감지를 위해 LOST 상태에서 수신한 첫 유효 패킷은 seq 비교 없이 기준값으로 삼는다.

## 10. 미확정 사항

| ID | 내용 |
|---|---|
| TBD-02 | class_id 목록 (6.2) |
| TBD-03 | 다중 경로점 일괄 전송 메시지 필요 여부 (고도화 단계 검토) |
