#pragma once

// IF-03 WebSocket 서버 (ICD 7장, ADR-003, REQ-COM-008).
//
// GW의 io 스레드 하나에서 돈다 (Gateway와 같은 스레드라 잠금이 없다).
// - 지정한 경로(/ws)의 업그레이드 요청만 받는다. 다른 요청에는 404로 답하고 닫는다.
// - broadcast()는 각 세션의 쓰기 대기열에 넣기만 한다. HMI가 뒤처지면 두 가지로 끊는다 (재접속하면
//   현재 상태부터 받는다): ① max_lag 안에 HMI에게서 pong 등 응답이 없을 때 (TCP 버퍼 크기와 무관한
//   지연 상한), ② 세션 대기열이 상한을 넘을 때 (메모리 상한). 느린 HMI 하나가 판정 주기·HEARTBEAT·
//   다른 HMI를 막지 못한다 (SDD 5장).
// - 동시 세션 수가 상한이면 새 업그레이드 요청에 503으로 답한다.
// - 새 세션에는 on_open이 돌려준 메시지(현재 link_status·stats)를 먼저 보낸다.
// - accept 오류(fd 고갈 등)는 기록하고 잠시 뒤 다시 받는다 (즉시 재시도하면 CPU를 태운다).

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ugvc::app {

class WsServer {
 public:
  using Text = std::shared_ptr<const std::string>;  // 세션들이 같은 문자열 하나를 공유한다
  struct Callbacks {
    std::function<std::vector<Text>()> on_open;          // 새 세션에 먼저 보낼 메시지
    std::function<void(const std::string&)> on_message;  // HMI가 보낸 텍스트 (IF-03 7.2)
    std::function<void(const std::string&)> log;         // 진단 한 줄 (접속·끊김·거절)
  };

  struct Limits {
    std::size_t max_queued_per_session;  // 넘치면 그 세션을 끊는다
    std::size_t max_sessions;            // 넘치는 업그레이드 요청은 503 (핸드셰이크 중인 연결도 센다)
    std::chrono::milliseconds max_lag;   // 이 시간 안에 HMI의 응답(pong 등)이 없으면 끊는다
  };

  WsServer(boost::asio::io_context& io, const boost::asio::ip::tcp::endpoint& endpoint, std::string path,
           Limits limits, Callbacks callbacks);
  ~WsServer();
  WsServer(const WsServer&) = delete;
  WsServer& operator=(const WsServer&) = delete;

  void broadcast(const Text& text);

  std::size_t session_count() const { return sessions_.size(); }
  std::uint64_t slow_closed() const { return slow_closed_; }  // 대기열이 넘쳐 끊은 세션 수 (누계)
  std::uint16_t port() const;                         // 실제로 연 포트 (0을 주면 OS가 고른 포트)

 private:
  class Session;
  friend class Session;

  void accept();

  boost::asio::ip::tcp::acceptor acceptor_;
  boost::asio::steady_timer retry_timer_;  // accept 오류 뒤 재시도 대기
  std::string path_;
  Limits limits_;
  Callbacks cb_;
  std::set<std::shared_ptr<Session>> sessions_;  // 핸드셰이크를 마친 세션만
  std::size_t handshaking_ = 0;                  // 상한 검사를 통과하고 핸드셰이크 중인 연결 수
  std::uint64_t slow_closed_ = 0;
};

}  // namespace ugvc::app
