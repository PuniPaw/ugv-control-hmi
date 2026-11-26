# UGVC — AI 표적 탐지 기반 UGV 원격 관제 시스템

실시간 텔레메트리와 AI 탐지 결과를 통합해 운용자가 빠르게 판단하도록 돕는 무인지상차량(UGV) 관제 HMI입니다.
요구사항 → 설계 → 구현 → 시험이 ID로 추적되는 방산형 개발 프로세스를 함께 보여 주는 것을 목표로 합니다.

> 🚧 개발 중 (MVP 목표: 2026-12-31)

## 구성

```
[SIM 차량 시뮬레이터, Python] ──UDP──▶ [GW 통신 게이트웨이, C++17] ──WebSocket──▶ [HMI 관제 화면, Vue 3]
[DET AI 탐지 노드, Python]   ──UDP──▶ [GW]                         [DET] ──WebSocket(영상)──▶ [HMI]
```

| 디렉터리 | 구성요소 | 상태 |
|---|---|---|
| `gateway/` | GW: 패킷 검증, 링크 상태 판정, HMI 중계, 명령 재전송 (C++17, CMake, GoogleTest) | 검증 규칙 ①~⑨, 링크 상태, 유실률, stats·HEARTBEAT 1Hz, IF-03 WebSocket 중계 |
| `sim/` | SIM: 경로점 주행 모사, 10Hz 텔레메트리, Failsafe (Python) | TELEMETRY 10Hz, COMMAND 수행·ACK, HEARTBEAT 감시·Failsafe. [sim/README](sim/README.md) |
| `detector/` | DET: 영상 재생, 객체 탐지, 결과·영상 송신 (Python) | 예정 |
| `hmi/` | HMI: 지도, 영상+박스, 상태 패널, 경고, 명령 (Vue 3 + TS) | IF-03 수신·재접속, 지도(위치·궤적), 상태 패널, 명령 패널. [hmi/README](hmi/README.md) |
| `python/` | `ugvc_proto`: SIM·DET 공용 Python codec (ICD 메시지 직렬화) | 헤더·CRC·TELEMETRY |
| `testdata/vectors/` | Python·C++가 함께 쓰는 바이트 테스트 벡터 | frame.json, telemetry.json |
| `tools/` | 벡터 생성기 등 보조 스크립트 | |
| `tests/integration/` | 구성요소를 실제로 띄워 보는 통합 시험 (pytest) | SIM → GW smoke |
| `docs/` | SRS, ICD, STP, ADR | |

## 문서

| 문서 | 내용 |
|---|---|
| [SRS](docs/01_SRS.md) | 소프트웨어 요구사항 (REQ-*) |
| [ICD](docs/02_ICD.md) | 인터페이스 정의 (메시지 형식, 검증 규칙) |
| [SDD](docs/03_SDD.md) | 설계 (GW 모듈·스레드, HMI 구조) |
| [STP](docs/04_STP.md) | 시험 계획 (TC-*) |
| [ADR](docs/adr/) | 설계 결정 기록 |
| [GLOSSARY](GLOSSARY.md) | 용어집 |

## 로컬 실행 (GW + SIM + HMI)

Ubuntu 24.04(WSL 포함) 기준입니다. GW·SIM·HMI를 각각 터미널에 띄우고 브라우저로 관제 화면을 봅니다.

### 준비 (처음 한 번)

| 도구 | 버전 | 용도 |
|---|---|---|
| CMake, Ninja, C++17 컴파일러 | CMake 3.20+, GCC 9+ | GW 빌드 |
| Boost 헤더 | 1.83+ | GW (Asio·Beast) |
| libpq 헤더 (`libpq-dev`) | PostgreSQL 12+ | GW 빌드 (DB 로그 저장) |
| PostgreSQL 서버 | 12+ (선택) | DB 로그 저장, DB 통합 시험. 없으면 그 시험만 skip |
| Python | 3.10+ | SIM, 시험 |
| Node.js | **22.12+** (nvm 권장) | HMI (vite, vitest) |

```bash
sudo apt-get install -y build-essential cmake ninja-build git libboost-dev libpq-dev python3 python3-venv
```

DB 로그 저장을 쓰거나 DB 통합 시험을 돌리려면 PostgreSQL도 설치합니다(선택):

```bash
sudo apt-get install -y postgresql postgresql-client
```

Node는 apt 패키지(Ubuntu 24.04는 v18)가 아니라 nvm으로 설치합니다. apt의 Node는 HMI 도구가 요구하는 버전보다 낮고, apt의 npm은 전역 설치(`npm install -g`)에 sudo가 필요합니다.

```bash
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.3/install.sh | bash
```

터미널을 새로 연 뒤:

```bash
nvm install 22 && nvm alias default 22
```

```bash
node -v && which node npm      # v22.x, 경로가 ~/.nvm/... 이어야 한다
```

### 빌드 (처음 한 번, 코드가 바뀌면 다시)

여기서는 아무것도 띄우지 않습니다. GW를 컴파일하고 HMI 의존성 패키지를 설치만 합니다. 저장소 루트에서:

```bash
cmake -S gateway -B gateway/build -G Ninja && cmake --build gateway/build
```

```bash
cd hmi && npm install   # HMI 의존성 설치 (실행 아님)
```

### 실행

터미널 세 개를 저장소 루트에서 열고 이 순서로 띄웁니다. SIM은 GW의 HEARTBEAT를 3초 넘게 받지 못하면 정지(Failsafe)하므로 **GW를 먼저** 띄웁니다.

```bash
gateway/build/ugvc_gateway                          # 터미널 1: GW
```

```bash
cd sim && PYTHONPATH=../python:. python3 -m ugvc_sim  # 터미널 2: SIM
```

```bash
cd hmi && npm run dev                               # 터미널 3: HMI
```

브라우저에서 http://127.0.0.1:5173 을 엽니다. HMI는 GW가 늦게 떠도 스스로 다시 접속하므로 HMI 순서는 엄격하지 않지만, GW → SIM 순서는 지켜야 합니다. SIM은 방위 87.25°로 직진하는 MISSION 모드로 시작합니다.

#### DB 로그 저장 (선택)

GW에 `--db <libpq 접속 문자열>`을 주면 TELEMETRY·DETECTION·이벤트(링크 전이, 명령)를 PostgreSQL에 저장합니다. 빈 DB를 하나 만들어 두면 GW가 접속하면서 테이블을 만듭니다(스키마는 SDD 5.1).

```bash
sudo -u postgres createuser -s "$USER" && createdb ugvc   # 처음 한 번
```

```bash
gateway/build/ugvc_gateway --db "dbname=ugvc"       # 터미널 1: GW (DB 저장 켬)
```

- `--db`가 없으면 저장하지 않고 그대로 중계합니다. DB가 꺼져 있거나 멈춰도 GW는 중계를 계속하고, 버린 기록 수를 stderr에 `db log dropped ...`로 남기며 다시 접속을 시도합니다.
- 접속 사용자에게 테이블 생성 권한이 필요합니다(GW가 접속할 때마다 `CREATE TABLE IF NOT EXISTS`를 실행). PostgreSQL 15부터는 DB 소유자로 접속하거나 `GRANT CREATE ON SCHEMA public`을 줍니다.
- 비밀번호는 명령줄에 넣지 말고 `~/.pgpass`나 `PGPASSWORD`로 줍니다(명령줄은 `ps`로 보입니다).
- 저장된 내용 보기: `psql ugvc -c "SELECT kind, source, detail FROM event ORDER BY id DESC LIMIT 10"`

### 화면에서 확인할 것

| 해 볼 것 | 보이는 것 |
|---|---|
| 그대로 둔다 | 지도에 차량 마커·진행 방향·궤적, 상태 패널에 차량 링크 CONNECTED, 유실률, 모드, 배터리, 속도, 위치 |
| 명령 패널에서 정지·복귀·경로점 이동 | 버튼은 확인 창만 연다. "보내기"를 눌러야 송신되고 결과(수락·거절)가 표시된다. 경로점을 보내면 차량이 그쪽으로 선회한다 |
| SIM 중지 (터미널 2에서 Ctrl+C) | 1초 뒤 차량 링크 LOST, 차량 값이 "이전 값"으로 흐려지고 명령 버튼이 사유와 함께 막힌다 |
| GW 중지 후 다시 실행 | "GW 연결 끊김 · N초 후 재접속" 뒤 자동 재접속 |
| GW만 중지하고 SIM은 둔다 | 3초 뒤 SIM이 STOPPED (터미널 2에 `failsafe` 줄). GW를 다시 띄워도 명령을 보내기 전까지 정지 상태 유지 |

아직 없는 것: 탐지 노드(DET)와 영상 오버레이(그래서 "탐지 노드"는 항상 LOST), 경고 체계, 경로점 목록 표시, 로그 DB. 지도 타일은 기본이 격자 배경이고 `VITE_TILE_URL`로 지정합니다([hmi/README](hmi/README.md)).

### GW 출력과 옵션

GW는 IF-03 메시지를 WebSocket `ws://127.0.0.1:8080/ws`(HMI용)와 stdout(한 줄에 JSON 하나)에 똑같이 보냅니다: 유효 TELEMETRY·DETECTION은 받는 즉시, 송신원별 `stats`·`link_status`는 1Hz와 상태 전이 시. HMI가 접속하면 현재 `stats`·`link_status`를 먼저 보냅니다. HEARTBEAT는 1Hz로 UDP 15010에 보냅니다.

다른 PC의 HMI가 접속하게 하려면 `--ws-host 0.0.0.0`, 포트를 바꾸려면 `--ws-port <port>`를 줍니다. 화면 없이 IF-03만 보려면(`pip install websockets` 필요):

```bash
python3 -m websockets ws://127.0.0.1:8080/ws
```

SIM 옵션(`--rate`, `--count`, `-v` 등)은 [sim/README](sim/README.md)에 있습니다.

### 자동 시연 점검 (선택)

위 시나리오를 실제 GW·SIM·HMI와 Playwright chromium으로 한 번에 확인합니다(STP 수준 D). 빈 포트를 골라 쓰므로 직접 띄운 프로세스와 충돌하지 않습니다. GW를 먼저 빌드해 두어야 합니다.

처음 한 번 (nvm의 Node 22에서):

```bash
npm install -g playwright && playwright install --with-deps chromium
```

`hmi/`에서:

```bash
NODE_PATH=$(npm root -g) node e2e/smoke.mjs --screenshot hmi.png
```

끝에 `PASS`가 나오면 통과입니다. 브라우저는 반드시 전역 `playwright` 명령으로 받습니다(버전이 다르면 브라우저를 찾지 못함).

### 문제 해결

| 증상 | 원인과 조치 |
|---|---|
| `Cannot find module 'playwright'` | playwright 미설치. 위 "자동 시연 점검"의 설치를 하고 `NODE_PATH=$(npm root -g)`를 붙여 실행 |
| `npm install -g`가 `EACCES ... /usr/local/lib/node_modules` | apt의 npm을 쓰고 있다. nvm의 Node로 바꾼다. `sudo npm install -g`는 쓰지 않는다 |
| `EBADENGINE ... current: { node: 'v18...' }` | apt의 Node v18이 잡혔다. `which -a node npm`으로 확인 |
| 어떤 터미널은 v22, 어떤 터미널은 v18 | nvm 설치 전에 연 터미널이라 nvm이 로드되지 않았다. `source ~/.bashrc && hash -r` 또는 새 터미널. nvm은 설치한 폴더와 관계없이 `~/.nvm`에 하나만 있다 |
| Node를 바꾼 뒤 `npm run dev`·`npm test`가 이상하다 | `hmi/`에서 `rm -rf node_modules && npm install` |
| 차량 링크 LOST | SIM이 꺼졌거나 다른 포트로 보내고 있다. 터미널 2가 살아 있는지 본다 |
| 차량 링크는 CONNECTED인데 STOPPED로 멈춰 있다 | Failsafe로 정지했다(GW보다 SIM을 먼저 띄웠거나 GW가 잠시 꺼졌음). 터미널 2에 `failsafe` 줄이 있다. 명령 패널에서 경로점 이동이나 복귀를 보내면 다시 주행한다 |

## 빌드와 시험 (gateway)

```bash
cd gateway
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

필요 도구: CMake 3.20+, C++17 컴파일러(GCC 9+ / Clang 10+ / MSVC 2019+), Boost 1.83+ 헤더(Asio·Beast), libpq 헤더(DB 로그 저장), Git(nlohmann/json·GoogleTest를 FetchContent로 받음).

Ubuntu에서 Boost·libpq 설치:

```bash
sudo apt-get install -y libboost-dev libpq-dev
```

## 정적 분석 (gateway)

```bash
tools/cppcheck.sh
```

`gateway/build/compile_commands.json`을 쓰므로 cmake configure를 먼저 해야 합니다. 외부 의존성 헤더는 읽지 않게 걸러서 우리 파일이 모두 검사되도록 합니다. 경고가 1건이라도 있으면 실패합니다. 예외는 `gateway/cppcheck-suppressions.txt`에 사유와 함께 적습니다 (REQ-QUAL-002).

## STP ↔ 테스트 이름 확인

```bash
python3 tools/check_stp_tests.py gateway/build/ugvc_tests
```

STP "테스트" 열의 이름이 실제 테스트에 없으면 실패합니다 (RTM을 grep으로 만들기 위한 전제). 통합 시험에도 포함되어 있습니다.

## 시험 (python)

설치 없이 저장소 안에서 바로 실행됩니다 (각 `pyproject.toml`의 `pythonpath` 설정).

```bash
python3 -m pip install pytest websockets
```

```bash
cd python && python3 -m pytest
```

```bash
cd sim && python3 -m pytest
```

통합 시험(gateway 빌드 필요):

```bash
cd tests && python3 -m pytest
```

DB 로그 시험(`test_db_log.py`)은 임시 PostgreSQL 클러스터를 띄웁니다. PostgreSQL 서버가 없으면 그 시험만 skip하고 요약 끝에 사유를 출력합니다(STP 2.8).

STP 통합 TC(10Hz 600개 × 3회, HEARTBEAT 주기)는 약 3.5분 걸려 기본 실행에서 빠집니다:

```bash
cd tests && python3 -m pytest -m slow
```

## 테스트 벡터 다시 만들기

```bash
python3 tools/vectors/gen_vectors.py
```

벡터는 우리 codec이 아니라 Python 표준 라이브러리(`struct`, `binascii.crc_hqx`)로 만들어서, C++·Python 구현 양쪽과 독립된 기준이 됩니다.
