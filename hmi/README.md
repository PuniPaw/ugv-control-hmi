# HMI — 관제 화면 (Vue 3 + TypeScript)

GW의 IF-03(WebSocket `ws://<호스트>:8080/ws`)을 받아 지도(위치·진행 방향·궤적)와 상태 패널(링크·유실률·배터리·모드)을 그린다.
GW 연결이 끊기면(닫힘, 접속 5초 초과, 4초 무수신) 1s→2s→4s→5s 간격으로 다시 접속하고, stats가 2초 넘게 끊기거나 늦으면 "GW 데이터 지연"을 표시한다. GW와 HMI가 다른 PC여도 시계를 맞출 필요가 없다 (ICD 7장, REQ-HMI-006).
명령 패널(정지·복귀·경로점 이동)은 운용자가 "보내기"로 확인한 뒤에만 IF-03 `command`를 보내고 `cmd_result`를 표시한다. 차량 링크가 LOST이거나 GW와 연결되지 않으면 버튼을 막고 사유를 보인다. 결과 전에 연결이 끊긴 명령은 "결과 미확인"으로 두고 자동으로 다시 보내지 않는다. 다시 시도는 새 명령으로 확인을 거쳐 새 req_id로 보내며, 같은 종류의 명령이 결과 전이면 확인 창이 경고한다 (REQ-CMD-001·004, ICD 7.2).
구조는 SDD 8장, 시험은 STP 3.10~3.13, 3.18·3.19.

## 실행

```bash
npm install
npm run dev          # http://127.0.0.1:5173
```

GW와 SIM을 함께 띄운다 (저장소 루트에서):

```bash
gateway/build/ugvc_gateway
```

```bash
cd sim && PYTHONPATH=../python:. python3 -m ugvc_sim.telemetry_sender
```

명령에 ACK를 받으려면 SIM 수신부도 띄운다:

```bash
cd sim && PYTHONPATH=../python:. python3 -m ugvc_sim.uplink
```

빌드 변수 (`.env.local` 또는 환경 변수):

| 변수 | 기본 | 설명 |
|---|---|---|
| `VITE_GW_URL` | `ws://<페이지 호스트>:8080/ws` | IF-03 주소 |
| `VITE_TILE_URL` | 없음(격자 배경만) | Leaflet 타일 주소, 예: `http://127.0.0.1:8090/{z}/{x}/{y}.png` |

## 시험

```bash
npm test             # vitest 단위 시험 (파서, 상태 갱신, 신선도, 재접속, 표시 정책, 궤적 그리기, 명령 패널)
npm run typecheck
```

시연 점검 (실제 GW·SIM·HMI + Playwright chromium, STP 수준 D):

```bash
node e2e/smoke.mjs --screenshot hmi.png
```

```bash
node e2e/smoke.mjs --clock-skew 3000      # HMI 시계를 GW보다 3초 빠르게 (다른 PC 배치)
```

`gateway/build/ugvc_gateway`가 빌드돼 있어야 하고 `playwright` 패키지가 필요하다 (전역 설치면 `NODE_PATH=$(npm root -g)`).

`tests/fixtures/gw_if03_sample.jsonl`은 실제 GW stdout에서 받은 IF-03 줄이다. GW의 IF-03 형식을 바꾸면 저장소 루트에서 다시 만든다:

```bash
PYTHONPATH=python python3 hmi/tests/fixtures/capture_gw_if03.py
```

시험 이름은 모두 REQ ID로 시작하고, 파일에 `describe`를 쓰지 않는다 (it 제목이 곧 STP의 시험 이름, `tools/check_stp_tests.py`가 비교).
