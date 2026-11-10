#pragma once

// ACK (msg_type 0x11, payload 5B) — ICD 6.4

#include <cstddef>
#include <cstdint>
#include <optional>

#include "ugvc/codec/bytes.hpp"
#include "ugvc/codec/msg_type.hpp"

namespace ugvc::codec {

inline constexpr std::size_t kAckPayloadSize = 5;

// 표 6-5 result
enum class AckResult : std::uint8_t {
  Accepted = 0,
  RejectedInvalid = 1,
  RejectedState = 2,
};

struct Ack {
  std::uint32_t cmd_id = 0;
  std::uint8_t result = 0;  // AckResult. 범위 밖 값도 받은 그대로 둔다 (검증 ⑧에서 판정)

  bool operator==(const Ack& o) const { return cmd_id == o.cmd_id && result == o.result; }
};

Bytes encode_ack(const Ack& a);
std::optional<Ack> decode_ack(const std::uint8_t* payload, std::size_t len) noexcept;

}  // namespace ugvc::codec
