#pragma once

// 수신 검증 규칙 ①~⑧ (ICD 9장). 상태가 없는 순수 함수.
// 규칙 ⑨(seq 순서)는 스트림별 상태가 필요하므로 SeqTracker가 맡는다.

#include <cstddef>
#include <cstdint>
#include <variant>

#include "ugvc/codec/ack.hpp"
#include "ugvc/codec/detection.hpp"
#include "ugvc/codec/frame.hpp"
#include "ugvc/codec/telemetry.hpp"
#include "ugvc/ingress/reject_reason.hpp"
#include "ugvc/ingress/source.hpp"

namespace ugvc::ingress {

// GW가 받는 메시지. 검증 단계에서 한 번 디코딩한 값을 그대로 넘긴다 (다시 디코딩하지 않는다).
using Message = std::variant<codec::Telemetry, codec::Detection, codec::Ack>;

// 규칙 ①~⑧을 통과한 프레임. 수신 버퍼와 무관한 값이라 버퍼가 재사용돼도 안전하다.
struct CheckedFrame {
  codec::Header header;
  Message message;
};

// 통과하면 CheckedFrame, 아니면 처음 실패한 규칙의 원인.
using CheckResult = std::variant<CheckedFrame, RejectReason>;

CheckResult check_frame(Source source, const std::uint8_t* data, std::size_t len);

}  // namespace ugvc::ingress
