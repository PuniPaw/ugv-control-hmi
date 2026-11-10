#include "ugvc/gateway/gateway.hpp"

#include <variant>

namespace ugvc::gateway {

using ingress::Source;

Gateway::Gateway(const core::IClock& clock, link::ILinkTransitionSink& sink)
    : clock_(clock),
      sources_{{PerSource(Source::Vehicle, clock, sink), PerSource(Source::Detector, clock, sink)}} {}

std::optional<ingress::CheckedFrame> Gateway::on_datagram(Source source, const std::uint8_t* data,
                                                          std::size_t len) {
  PerSource& s = of(source);

  const ingress::CheckResult checked = ingress::check_frame(source, data, len);
  if (const auto* reason = std::get_if<ingress::RejectReason>(&checked)) {
    s.errors.add(*reason);
    return std::nullopt;
  }
  const auto& frame = std::get<ingress::CheckedFrame>(checked);

  s.link.poll();
  if (s.link.state() == link::LinkState::Lost) {
    for (auto& [type, tracker] : s.streams) tracker.rebase();
  }

  const ingress::SeqTracker::Result seq = s.streams[frame.header.msg_type].observe(frame.header.seq);
  if (!seq.in_order) {
    s.errors.add(ingress::RejectReason::Order);
    return std::nullopt;
  }

  ++s.rx_count;
  s.lost_count += seq.lost;
  s.loss.record(clock_.now(), 1, seq.lost);
  s.link.on_valid_packet();
  return frame;
}

void Gateway::poll() {
  for (auto& s : sources_) s.link.poll();
}

SourceStats Gateway::stats(Source source) const {
  const PerSource& s = of(source);
  return {s.rx_count, s.lost_count, s.loss.rate(clock_.now()), s.errors};
}

}  // namespace ugvc::gateway
