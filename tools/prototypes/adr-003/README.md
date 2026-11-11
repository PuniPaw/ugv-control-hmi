# ADR-003 시제품 (버리는 코드)

ADR-003(WebSocket 라이브러리 선택)의 근거를 다시 돌려 보기 위한 시제품이다. GW 빌드에는 포함되지 않는다.

과제: 단일 스레드에서 100ms마다 JSON을 모든 클라이언트에 브로드캐스트하고, 클라이언트의 텍스트 메시지를 받는다.
확인: `client.py`가 클라이언트 2개로 1초 동안 받은 메시지 수·간격과 명령 1개 송신을 출력한다 (`pip install websockets` 필요).

## A. Boost.Beast (`libboost-dev` 필요)

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wconversion -Werror beast_server.cpp -o beast_server -pthread
```

```bash
./beast_server &
python3 client.py ws://127.0.0.1:18080
```

## B. uWebSockets v20.70.0

```bash
git clone --depth 1 --branch v20.70.0 --recurse-submodules --shallow-submodules https://github.com/uNetworking/uWebSockets.git uws
make -C uws/uSockets
g++ -std=c++20 -O2 -isystem uws/src -isystem uws/uSockets/src uws_server.cpp uws/uSockets/uSockets.a -lz -o uws_server
```

```bash
./uws_server &
python3 client.py ws://127.0.0.1:18081
```

## C. websocketpp 0.8.2 (컴파일 확인만)

GW와 같은 `ASIO_NO_DEPRECATED`에서 `io_service`가 없어 컴파일되지 않는 것을 확인한다.

```bash
git clone --depth 1 --branch 0.8.2 https://github.com/zaphoyd/websocketpp.git wspp
g++ -std=c++17 -DASIO_NO_DEPRECATED -isystem wspp -isystem <standalone asio>/asio/include websocketpp_compile_check.cpp -pthread
```

## 결과 (2026-11-11, Ubuntu 24.04, GCC 13.3)

| | A | B | C |
|---|---|---|---|
| client.py | 클라이언트당 11개, 100ms, 명령 수신 | 클라이언트당 11개, 100ms, 명령 수신 | 컴파일 실패 |
| 빌드 / 실행 파일 | 13.8s / 1.26MB | 3.6s / 173KB | — |
