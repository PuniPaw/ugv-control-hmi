#pragma once

// IF-03 WebSocket 서버 (ICD 7장, ADR-003, REQ-COM-008).
//
// GW의 io 스레드 하나에서 돈다 (Gateway와 같은 스레드라 잠금이 없다).
// - 지정한 경로(/ws)의 업그레이드 요청만 받는다. 다른 요청에는 404로 답하고 닫는다.
// - broadcast()는 각 세션의 쓰기 대기열에 넣기만 한다. 세션 대기열이 상한을 넘으면 그 세션 몫만
//   버리고 센다. 느린 HMI 하나가 판정 주기·HEARTBEAT·다른 HMI를 막지 못하게 한다 (SDD 5장).
// - 새 세션에는 on_open이 돌려준 메시지(현재 link_status·stats)를 먼저 보낸다.

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

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

  WsServer(boost::asio::io_context& io, const boost::asio::ip::tcp::endpoint& endpoint, std::string path,
           std::size_t max_queued_per_session, Callbacks callbacks);
  ~WsServer();
  WsServer(const WsServer&) = delete;
  WsServer& operator=(const WsServer&) = delete;

  void broadcast(const Text& text);

  std::size_t session_count() const { return sessions_.size(); }
  std::uint64_t dropped() const { return dropped_; }  // 모든 세션에서 대기열 초과로 버린 메시지 수
  std::uint16_t port() const;                         // 실제로 연 포트 (0을 주면 OS가 고른 포트)

 private:
  class Session;
  friend class Session;

  void accept();

  boost::asio::ip::tcp::acceptor acceptor_;
  std::string path_;
  std::size_t max_queued_;
  Callbacks cb_;
  std::set<std::shared_ptr<Session>> sessions_;  // 핸드셰이크를 마친 세션만
  std::uint64_t dropped_ = 0;
};

}  // namespace ugvc::app
