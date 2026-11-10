#pragma once

// testdata/vectors/*.json 로더 (STP 2.4). 시험 코드 전용.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef UGVC_TESTDATA_DIR
#error "UGVC_TESTDATA_DIR must be defined by CMake"
#endif

namespace ugvc::test {

inline nlohmann::json load_vectors(const std::string& file) {
  const std::string path = std::string(UGVC_TESTDATA_DIR) + "/vectors/" + file;
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open test vectors: " + path);
  }
  return nlohmann::json::parse(in);
}

inline std::vector<std::uint8_t> from_hex(const std::string& hex) {
  if (hex.size() % 2 != 0) {
    throw std::invalid_argument("odd-length hex: " + hex);
  }
  std::vector<std::uint8_t> out;
  out.reserve(hex.size() / 2);
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    out.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  }
  return out;
}

}  // namespace ugvc::test
