# HMI — 관제 화면 (Vue 3 + TypeScript)

GW의 IF-03(WebSocket `ws://<호스트>:8080/ws`)을 받아 지도(위치·진행 방향·궤적)와 상태 패널(링크·유실률·배터리·모드)을 그린다.
GW 연결이 끊기면 1s→2s→4s→5s 간격으로 다시 접속하고, stats가 2초 넘게 끊기거나 늦으면 "GW 데이터 지연"을 표시한다 (ICD 7장).
구조는 SDD 8장.

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

빌드 변수 (`.env.local` 또는 환경 변수):

| 변수 | 기본 | 설명 |
|---|---|---|
| `VITE_GW_URL` | `ws://<페이지 호스트>:8080/ws` | IF-03 주소 |
| `VITE_TILE_URL` | 없음(격자 배경만) | Leaflet 타일 주소, 예: `http://127.0.0.1:8090/{z}/{x}/{y}.png` |

## 시험

```bash
npm test             # vitest 단위 시험 (파서, 상태 갱신, 신선도, 재접속)
npm run typecheck
```

시연 점검 (실제 GW·SIM·HMI + Playwright chromium, STP 수준 D):

```bash
node e2e/smoke.mjs --screenshot hmi.png
```

`gateway/build/ugvc_gateway`가 빌드돼 있어야 하고 `playwright` 패키지가 필요하다 (전역 설치면 `NODE_PATH=$(npm root -g)`).

`tests/fixtures/gw_if03_sample.jsonl`은 실제 GW stdout에서 받은 IF-03 줄이다. GW의 IF-03 형식을 바꾸면 다시 받아 둔다.
