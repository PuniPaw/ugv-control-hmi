#include "ugvc/if03/command.hpp"

#include <cmath>

#include <nlohmann/json.hpp>

namespace ugvc::if03 {
namespace {

using nlohmann::json;
using Result = std::variant<CommandRequest, CommandParseError>;

Result fail(std::string reason, std::optional<std::string> req_id = std::nullopt) {
  return CommandParseError{std::move(req_id), std::move(reason)};
}

// 도(deg) → 1e-7도 정수. 범위는 변환 전에 도 단위로 검사한다.
std::optional<std::int32_t> to_e7(const json& data, const char* key, double limit) {
  const auto it = data.find(key);
  if (it == data.end() || !it->is_number()) return std::nullopt;
  const double deg = it->get<double>();
  if (!std::isfinite(deg) || deg < -limit || deg > limit) return std::nullopt;
  return static_cast<std::int32_t>(std::llround(deg * 1e7));
}

}  // namespace

Result parse_command(std::string_view text) {
  if (text.size() > kMaxCommandTextLen) return fail("message too long");
  const json msg = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (!msg.is_object()) return fail("not a JSON object");
  const auto type = msg.find("type");
  if (type == msg.end() || !type->is_string() || type->get<std::string>() != "command") {
    return fail("type is not 'command'");
  }
  const auto data_it = msg.find("data");
  if (data_it == msg.end() || !data_it->is_object()) return fail("data is not an object");
  const json& data = *data_it;

  const auto id_it = data.find("req_id");
  if (id_it == data.end() || !id_it->is_string()) return fail("req_id missing or not a string");
  const auto& req_id = id_it->get_ref<const std::string&>();
  if (req_id.empty() || req_id.size() > kMaxReqIdLen + 1) return fail("req_id must be 1-64 bytes");

  const auto cmd_it = data.find("cmd_type");
  if (cmd_it == data.end() || !cmd_it->is_string()) return fail("cmd_type missing or not a string", req_id);
  const auto cmd_type = codec::parse_cmd_type(cmd_it->get_ref<const std::string&>());
  if (!cmd_type) return fail("unknown cmd_type", req_id);

  CommandRequest req{req_id, *cmd_type, 0, 0};
  if (*cmd_type == codec::CmdType::Waypoint) {
    const auto lat = to_e7(data, "lat", 90.0);
    const auto lon = to_e7(data, "lon", 180.0);
    if (!lat || !lon) return fail("WAYPOINT needs finite lat in [-90, 90] and lon in [-180, 180]", req_id);
    req.lat = *lat;
    req.lon = *lon;
  }
  return req;
}

}  // namespace ugvc::if03
