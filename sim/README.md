# SIM — 차량 시뮬레이터 (Python)

경로점 주행 모사, TELEMETRY 10Hz 송신(IF-01a), COMMAND 수행·ACK, HEARTBEAT 두절 시 Failsafe.

## 현재

- `ugvc_sim/vehicle.py`: 고정 방위 직선 주행, 배터리 감소, BATTERY_LOW 플래그. 공학 단위 → ICD LSB 변환
- `ugvc_sim/telemetry_sender.py`: TELEMETRY를 UDP 15001로 10Hz 송신 (절대 시각 스케줄)
- `ugvc_sim/uplink.py`: UDP 15010(IF-01b)에서 COMMAND·HEARTBEAT 수신·검증·해석, 마지막 HEARTBEAT 후 경과 시간(`HeartbeatWatch`)

```bash
PYTHONPATH=../python:. python3 -m ugvc_sim.telemetry_sender --count 600 -v
```

```bash
PYTHONPATH=../python:. python3 -m ugvc_sim.uplink
```

## 예정

경로점 주행, 명령 실행·ACK 응답(중복 cmd_id 재실행 금지), HEARTBEAT 3.0s 두절 시 Failsafe (`HeartbeatWatch.expired` 사용)
