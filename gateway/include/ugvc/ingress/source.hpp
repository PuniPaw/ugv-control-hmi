#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ugvc::ingress {

// 송신원 (GLOSSARY): GW에 바이너리 메시지를 보내는 노드. 수신 포트로 구분한다.
enum class Source : std::uint8_t {
  Vehicle,   // SIM, UDP 15001 (IF-01a)
  Detector,  // DET, UDP 15002 (IF-02)
};

inline constexpr std::array<Source, 2> kAllSources{Source::Vehicle, Source::Detector};
inline constexpr std::size_t kSourceCount = kAllSources.size();

constexpr std::size_t index_of(Source s) { return static_cast<std::size_t>(s); }

constexpr const char* to_string(Source s) {
  return s == Source::Vehicle ? "VEHICLE" : "DETECTOR";
}

}  // namespace ugvc::ingress
