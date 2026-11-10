#pragma once

// 수신 검증 규칙 ①~⑧ (ICD 9장). 상태가 없는 순수 함수.
// 규칙 ⑨(seq 순서)는 스트림별 상태가 필요하므로 SeqTracker가 맡는다.

#include <cstddef>
#include <cstdint>
#include <variant>

#include "ugvc/codec/frame.hpp"
#include "ugvc/ingress/reject_reason.hpp"
#include "ugvc/ingress/source.hpp"

namespace ugvc::ingress {

// 규칙 ①~⑧을 통과한 프레임. payload는 원본 버퍼를 가리킨다 (복사 없음, 버퍼보다 오래 쓰지 말 것).
struct CheckedFrame {
  codec::Header header;
  const std::uint8_t* payload = nullptr;
};

// 통과하면 CheckedFrame, 아니면 처음 실패한 규칙의 원인.
using CheckResult = std::variant<CheckedFrame, RejectReason>;

CheckResult check_frame(Source source, const std::uint8_t* data, std::size_t len);

}  // namespace ugvc::ingress
