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
| `gateway/` | GW: 패킷 검증, 링크 상태 판정, HMI 중계, 명령 재전송 (C++17, CMake, GoogleTest) | codec 작업 중 |
| `sim/` | SIM: 경로점 주행 모사, 10Hz 텔레메트리, Failsafe (Python) | 예정 |
| `detector/` | DET: 영상 재생, 객체 탐지, 결과·영상 송신 (Python) | 예정 |
| `hmi/` | HMI: 지도, 영상+박스, 상태 패널, 경고, 명령 (Vue 3 + TS) | 예정 |
| `testdata/vectors/` | Python·C++가 함께 쓰는 바이트 테스트 벡터 | 예정 |
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

필요 도구: CMake 3.20+, C++17 컴파일러(GCC 9+ / Clang 10+ / MSVC 2019+), Git(GoogleTest를 FetchContent로 받음).
