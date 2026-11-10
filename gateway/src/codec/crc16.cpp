#include "ugvc/codec/crc16.hpp"

namespace ugvc::codec {

std::uint16_t crc16_ccitt_false(const std::uint8_t* data, std::size_t len) noexcept {
  std::uint16_t crc = 0xFFFF;
  for (std::size_t i = 0; i < len; ++i) {
    // 바이트를 CRC 상위 8비트에 XOR한 뒤, 비트 단위로 8번 나눗셈(시프트 + 조건부 XOR)
    crc = static_cast<std::uint16_t>(crc ^ (static_cast<std::uint16_t>(data[i]) << 8));
    for (int bit = 0; bit < 8; ++bit) {
      if (crc & 0x8000U) {
        crc = static_cast<std::uint16_t>((crc << 1) ^ 0x1021U);
      } else {
        crc = static_cast<std::uint16_t>(crc << 1);
      }
    }
  }
  return crc;
}

}  // namespace ugvc::codec
