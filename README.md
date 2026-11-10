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
| `gateway/` | GW: 패킷 검증, 링크 상태 판정, HMI 중계, 명령 재전송 (C++17, CMake, GoogleTest) | 검증 규칙 ①~⑨, 링크 상태, 유실률, stats·HEARTBEAT 1Hz, IF-03 JSON Lines 출력 |
| `sim/` | SIM: 경로점 주행 모사, 10Hz 텔레메트리, Failsafe (Python) | 직선 주행 TELEMETRY 10Hz 송신 |
| `detector/` | DET: 영상 재생, 객체 탐지, 결과·영상 송신 (Python) | 예정 |
| `hmi/` | HMI: 지도, 영상+박스, 상태 패널, 경고, 명령 (Vue 3 + TS) | 예정 |
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
| [ADR](docs/adr/) | 설계 결정 기록 |
| [GLOSSARY](GLOSSARY.md) | 용어집 |

## 빌드와 시험 (gateway)

```bash
cd gateway
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

필요 도구: CMake 3.20+, C++17 컴파일러(GCC 9+ / Clang 10+ / MSVC 2019+), Git(Asio·nlohmann/json·GoogleTest를 FetchContent로 받음).

## 정적 분석 (gateway)

```bash
tools/cppcheck.sh
```

`gateway/build/compile_commands.json`을 쓰므로 cmake configure를 먼저 해야 합니다. 경고가 1건이라도 있으면 실패합니다. 예외는 `gateway/cppcheck-suppressions.txt`에 사유와 함께 적습니다 (REQ-QUAL-002).

## 실행: SIM → GW

```bash
./gateway/build/ugvc_gateway
```

다른 터미널에서:

```bash
cd sim
PYTHONPATH=../python:. python3 -m ugvc_sim.telemetry_sender
```

GW는 IF-03 형식 JSON을 한 줄씩 stdout에 출력합니다: 유효 TELEMETRY·DETECTION은 받는 즉시, 송신원별 `stats`·`link_status`는 1Hz와 상태 전이 시. HEARTBEAT는 1Hz로 UDP 15010에 보냅니다.

## 시험 (python)

설치 없이 저장소 안에서 바로 실행됩니다 (각 `pyproject.toml`의 `pythonpath` 설정).

```bash
python3 -m pip install pytest
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

STP 통합 TC(10Hz 600개 × 3회, HEARTBEAT 주기)는 약 3.5분 걸려 기본 실행에서 빠집니다:

```bash
cd tests && python3 -m pytest -m slow
```

## 테스트 벡터 다시 만들기

```bash
python3 tools/vectors/gen_vectors.py
```

벡터는 우리 codec이 아니라 Python 표준 라이브러리(`struct`, `binascii.crc_hqx`)로 만들어서, C++·Python 구현 양쪽과 독립된 기준이 됩니다.
