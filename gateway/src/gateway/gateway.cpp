#include "ugvc/gateway/gateway.hpp"

#include <utility>
#include <variant>

#include "ugvc/codec/msg_type.hpp"

namespace ugvc::gateway {

using ingress::Source;

Gateway::Gateway(const core::IClock& clock) : clock_(clock), sources_{{PerSource(clock), PerSource(clock)}} {}

std::optional<Accepted> Gateway::on_datagram(Source source, const std::uint8_t* data, std::size_t len) {
  PerSource& s = of(source);

  ingress::CheckResult checked = ingress::check_frame(source, data, len);
  if (const auto* reason = std::get_if<ingress::RejectReason>(&checked)) {
    s.errors.add(*reason);
    return std::nullopt;
  }
  auto& frame = std::get<ingress::CheckedFrame>(checked);

  note(source, s.link.poll());
  if (s.link.state() == link::LinkState::Lost) {
    for (auto& [type, tracker] : s.streams) tracker.rebase();
  }

  // 규칙 ⑨는 ACK 스트림에 적용하지 않는다 (ICD 9장): ACK는 cmd_id로 짝을 찾고, 늦거나 중복된 ACK는
  // CommandTracker가 처리한다. SIM 수신부만 다시 시작해 ACK seq가 0부터 시작해도 버리지 않기 위해서다.
  ingress::SeqTracker::Result seq{true, 0};
  if (frame.header.msg_type != codec::kMsgAck) {
    seq = s.streams[frame.header.msg_type].observe(frame.header.seq);
    if (!seq.in_order) {
      s.errors.add(ingress::RejectReason::Order);
      return std::nullopt;
    }
  }

  ++s.rx_count;
  s.lost_count += seq.lost;
  s.loss.record(clock_.now(), 1, seq.lost);
  note(source, s.link.on_valid_packet());
  return Accepted{std::move(frame), *s.link.last_rx_epoch_ms()};
}

void Gateway::poll() {
  for (const Source src : ingress::kAllSources) note(src, of(src).link.poll());
}

SourceStats Gateway::stats(Source source) const {
  const PerSource& s = of(source);
  return {s.rx_count, s.lost_count, s.loss.rate(clock_.now()), s.errors};
}

}  // namespace ugvc::gateway
