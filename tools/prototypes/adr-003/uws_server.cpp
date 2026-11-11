// ADR-003 시제품 B. 지정 초기화자(.open = ...) 때문에 C++20으로 빌드한다 (라이브러리 자체는 C++17로도 빌드됨).
// 시제품 B: uWebSockets(기본 epoll 루프). 100ms마다 pub/sub로 브로드캐스트, 클라이언트 메시지 출력.
#include "App.h"

#include <iostream>
#include <string>

struct PerSocket {};

int main() {
  int n = 0;
  uWS::App app;
  app.ws<PerSocket>("/*", {
      .open = [](auto* ws) { ws->subscribe("ifo3"); },
      .message = [](auto*, std::string_view msg, uWS::OpCode) { std::cout << "rx: " << msg << std::endl; },
  });
  app.listen(18081, [](auto* s) { if (!s) std::cerr << "listen failed\n"; });
  // 100ms 타이머: uSockets 타이머 API (C)
  struct us_loop_t* loop = (struct us_loop_t*)uWS::Loop::get();
  struct us_timer_t* t = us_create_timer(loop, 0, sizeof(void*));
  static uWS::App* g_app = &app;
  static int* g_n = &n;
  us_timer_set(t, [](us_timer_t*) {
    std::string m = "{\"type\":\"telemetry\",\"seq\":" + std::to_string((*g_n)++) + "}";
    g_app->publish("ifo3", m, uWS::OpCode::TEXT);
  }, 100, 100);
  app.run();
}
