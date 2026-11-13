# SIM — 차량 시뮬레이터 (Python)

경로점 주행 모사, TELEMETRY 10Hz 송신(IF-01a), COMMAND 수행·ACK, HEARTBEAT 두절 시 Failsafe.

## 현재

- `ugvc_sim/vehicle.py`: 고정 방위 직선 주행, 배터리 감소, BATTERY_LOW 플래그. 공학 단위 → ICD LSB 변환
- `ugvc_sim/telemetry_sender.py`: TELEMETRY를 UDP 15001로 10Hz 송신 (절대 시각 스케줄)
- `ugvc_sim/uplink.py`: UDP 15010(IF-01b)에서 COMMAND·HEARTBEAT 수신·검증·해석, 마지막 HEARTBEAT 후 경과 시간(`HeartbeatWatch`)
  - COMMAND에 ACK 응답(IF-01a, GW 15001): 정상 `ACCEPTED`, 필드 범위 밖(cmd_id ≥ 1) `REJECTED_INVALID`(실행 안 함), cmd_id 0은 폐기(ACK 없음). 같은 cmd_id는 첫 결과를 그대로 다시 ACK하고 재실행하지 않는다(`CommandResponder`, 최근 1024개 기억). 차량 거동 적용은 아직 없다

```bash
PYTHONPATH=../python:. python3 -m ugvc_sim.telemetry_sender --count 600 -v
```

```bash
PYTHONPATH=../python:. python3 -m ugvc_sim.uplink [--gw-host 127.0.0.1] [--gw-port 15001] [--drop-first N]
```

`--drop-first N`은 시험용이다. 처음 받은 COMMAND N개를 ACK 없이 무시해 GW 재전송을 확인한다.

## 예정

경로점 주행, 명령 실행(차량 거동 적용), HEARTBEAT 3.0s 두절 시 Failsafe (`HeartbeatWatch.expired` 사용)
