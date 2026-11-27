# SIM — 차량 시뮬레이터 (Python)

경로점 주행 모사, TELEMETRY 10Hz·남은 경로 ROUTE 송신(IF-01a), COMMAND 수행·ACK, HEARTBEAT 두절 시 Failsafe. 한 프로세스(`python -m ugvc_sim`)다.

## 구성

- `ugvc_sim/app.py`, `__main__.py`: 실행 본체. UDP 소켓 하나(기본 15010)에서 COMMAND·HEARTBEAT를 받고, 절대 시각 스케줄로 10Hz TELEMETRY를 GW(기본 127.0.0.1:15001)로 보낸다. ACK와 ROUTE도 같은 GW 주소로 간다. 단일 스레드
- `ugvc_sim/simulator.py`: 소켓·시계 없는 순수 핵심 `Simulator`. 사건(명령, HEARTBEAT, tick)마다 `now`를 받고, 맨 앞에서 Failsafe 조건부터 평가한다. cmd_id별 첫 결과를 기억해(최근 1024개) 같은 cmd_id는 같은 ACK만 다시 보내고 재실행하지 않는다. `RouteReporter`는 ROUTE를 보낼 때(경로가 바뀌면 바로, 같아도 1.0초마다)를 정한다
- `ugvc_sim/uplink.py`: IF-01b 데이터그램 검증·해석(`parse_uplink`), 마지막 HEARTBEAT 후 경과 시간(`HeartbeatWatch`)
- `ugvc_sim/vehicle.py`: 불변 `VehicleState`와 순수 함수 `step`·`apply_command`·`enter_failsafe`, 공학 단위 → ICD LSB 변환

## 동작 규칙

- **STANDBY 기동 (REQ-SAFE-003)**: 시작 위치에 STANDBY·속도 0·경로 없음으로 서 있고, WAYPOINT·RTB를 수락해야 움직인다
- **남은 경로 보고 (REQ-CMD-007)**: 남은 경로점 목록을 ROUTE로 경로가 바뀐 그 처리 안에, 바뀌지 않아도 1Hz로 보낸다. 경로점은 32개까지이고, 32개일 때 받은 WAYPOINT는 `REJECTED_STATE`
- **Failsafe (REQ-SAFE-001)**: 마지막 HEARTBEAT(없으면 시작) 후 3.0초 이상이면 어떤 모드에서든 STOPPED, 속도 0, `FAILSAFE_ACTIVE`. 한 번의 두절에 한 번만 전이하고 정지 중에는 위치가 변하지 않는다
- **복귀해도 재개 없음 (REQ-SAFE-002)**: HEARTBEAT가 돌아와도 정지·플래그 유지. 링크가 살아 있을 때 새 cmd_id의 유효한 명령이 오면 수락·적용하고 플래그를 지운다
  - STOP: STOPPED, 속도 0. RTB: 순항 속도로 시작 위치까지 귀환 후 STANDBY. WAYPOINT: 경로 끝에 더하고 MISSION, 맨 앞 점으로 즉시 선회해 주행, 경로가 비면 STANDBY
  - 두절 중(HEARTBEAT 3.0초 이상 미수신)에는 STOP만 수락(상태 변화 없음), RTB·WAYPOINT는 `REJECTED_STATE`로 거절하고 실행하지 않는다
- STOP·RTB는 진행 중이던 경로를 취소하고, Failsafe 진입도 경로를 버린다

## 실행

```bash
PYTHONPATH=../python:. python3 -m ugvc_sim [--port 15010] [--gw-host 127.0.0.1] [--gw-port 15001] [--rate 10] [--count N] [-v]
```

GW의 HEARTBEAT(UDP 15010)가 오지 않으면 3초 뒤 정지한다. 표준출력은 JSON 한 줄씩(`command`, `heartbeat`, `reject`, `drop`, 모드·Failsafe 전이 `mode`·`failsafe`)이고 TELEMETRY는 `-v`일 때만 찍는다.

`--drop-first N`은 시험용이다. 처음 받은 COMMAND N개를 ACK 없이 무시해 GW 재전송을 확인한다.
