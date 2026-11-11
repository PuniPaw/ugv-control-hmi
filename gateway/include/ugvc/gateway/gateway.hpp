#pragma once

// GW 수신 처리의 중심 (소켓 없음, 순수 로직). 실행 파일은 받은 datagram과 50ms 판정 주기만 전달한다.
//
// 데이터그램 하나의 처리 순서:
//   1) 규칙 ①~⑧ (check_frame)          실패 → 원인 계수 +1, 폐기
//   2) 링크 상태 재판정, LOST면 그 송신원의 모든 스트림 seq를 rebase (송신원 재시작 대비)
//   3) 규칙 ⑨ (스트림의 SeqTracker)    실패 → order +1, 폐기
//   4) 유효 패킷: 수신 계수, 유실률 창, 링크 상태 갱신 → 호출자에게 프레임과 수신 시각 반환
//
// 링크 전이는 송신원을 붙여 모아 두고, 호출자가 take_transitions()로 꺼낸다.

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "ugvc/core/clock.hpp"
#include "ugvc/ingress/reject_reason.hpp"
#include "ugvc/ingress/seq_tracker.hpp"
#include "ugvc/ingress/source.hpp"
#include "ugvc/ingress/validator.hpp"
#include "ugvc/link/link_monitor.hpp"
#include "ugvc/link/loss_window.hpp"

namespace ugvc::gateway {

// IF-03 stats 한 건의 내용 (ICD 7.1)
struct SourceStats {
  std::uint64_t rx_count = 0;           // 유효 패킷 누계
  std::uint64_t lost_count = 0;         // 유실 누계
  std::optional<double> loss_rate_10s;  // 최근 10초, 비면 nullopt
  ingress::ErrorCounters errors;
};

// 한 송신원의 링크 상태 전이
struct LinkTransition {
  ingress::Source source;
  link::LinkChange change;
};

// 유효 패킷: 검증·디코딩된 프레임과 수신 시각 (링크 판정과 IF-03 rx_ts가 같은 값을 쓴다)
struct Accepted {
  ingress::CheckedFrame frame;
  std::uint64_t rx_epoch_ms;
};

class Gateway {
 public:
  explicit Gateway(const core::IClock& clock);

  // 유효 패킷이면 Accepted, 폐기되면 nullopt.
  std::optional<Accepted> on_datagram(ingress::Source source, const std::uint8_t* data, std::size_t len);

  // 판정 주기마다 호출: 모든 송신원의 링크 상태를 다시 판정한다.
  void poll();

  // on_datagram·poll에서 생긴 링크 전이를 일어난 순서대로 꺼낸다. 꺼내면 비워진다.
  std::vector<LinkTransition> take_transitions() { return std::exchange(transitions_, {}); }

  SourceStats stats(ingress::Source source) const;
  const link::LinkMonitor& link(ingress::Source source) const { return of(source).link; }

 private:
  // 스트림(송신원, msg_type)별 seq 상태. 송신원 안에서 msg_type으로 찾는다.
  struct PerSource {
    explicit PerSource(const core::IClock& clock) : link(clock) {}

    link::LinkMonitor link;
    std::map<std::uint8_t, ingress::SeqTracker> streams;
    link::LossWindow loss;
    std::uint64_t rx_count = 0;
    std::uint64_t lost_count = 0;
    ingress::ErrorCounters errors;
  };

  PerSource& of(ingress::Source s) { return sources_[ingress::index_of(s)]; }
  const PerSource& of(ingress::Source s) const { return sources_[ingress::index_of(s)]; }

  void note(ingress::Source s, std::optional<link::LinkChange> c) {
    if (c) transitions_.push_back({s, *c});
  }

  const core::IClock& clock_;
  std::array<PerSource, ingress::kSourceCount> sources_;
  std::vector<LinkTransition> transitions_;
};

}  // namespace ugvc::gateway
