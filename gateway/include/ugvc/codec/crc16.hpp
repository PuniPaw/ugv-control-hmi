#pragma once

#include <cstddef>
#include <cstdint>

namespace ugvc::codec {

// CRC-16/CCITT-FALSE (ICD 3장)
// poly 0x1021, init 0xFFFF, 입력·출력 반사 없음, 최종 XOR 0x0000
// 범위: 헤더 시작부터 페이로드 끝까지 (CRC 필드 제외)
std::uint16_t crc16_ccitt_false(const std::uint8_t* data, std::size_t len) noexcept;

}  // namespace ugvc::codec
