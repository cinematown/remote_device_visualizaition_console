#include "rdvc/protocol/status_parser.hpp"

#include <charconv>
#include <string>

namespace rdvc {
namespace {

constexpr std::string_view kStatusPrefix = "STATUS";

bool parse_int(std::string_view value, int& output)
{
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto result = std::from_chars(first, last, output);
    return result.ec == std::errc{} && result.ptr == last;
}

bool parse_double(std::string_view value, double& output)
{
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto result = std::from_chars(first, last, output);
    return result.ec == std::errc{} && result.ptr == last;
}

bool assign_field(DeviceStatus& status, std::string_view key, std::string_view value)
{
    if (key == "device_id") {
        status.device_id = std::string(value);
        return true;
    }

    if (key == "state") {
        status.state = std::string(value);
        return true;
    }

    if (key == "battery") {
        return parse_int(value, status.battery_percent);
    }

    if (key == "x") {
        return parse_double(value, status.x);
    }

    if (key == "y") {
        return parse_double(value, status.y);
    }

    if (key == "z") {
        return parse_double(value, status.z);
    }

    return true;
}

} // namespace

std::optional<DeviceStatus> parse_status_line(std::string_view line)
{
    if (!line.empty() && line.back() == '\n') {
        line.remove_suffix(1);
    }

    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }

    if (!line.starts_with(kStatusPrefix)) {
        return std::nullopt;
    }

    if (line.size() > kStatusPrefix.size() && line[kStatusPrefix.size()] != ' ') {
        return std::nullopt;
    }

    DeviceStatus status{};
    std::size_t position = kStatusPrefix.size();

    while (position < line.size()) {
        if (line[position] == ' ') {
            ++position;
            continue;
        }

        const auto next_space = line.find(' ', position);
        const auto token_end =
            next_space == std::string_view::npos ? line.size() : next_space;
        const auto token = line.substr(position, token_end - position);
        const auto equals = token.find('=');

        if (equals == std::string_view::npos || equals == 0
            || equals + 1 == token.size()) {
            return std::nullopt;
        }

        if (!assign_field(
                status, token.substr(0, equals), token.substr(equals + 1))) {
            return std::nullopt;
        }

        position = token_end;
    }

    if (status.device_id.empty() || status.state.empty()) {
        return std::nullopt;
    }

    return status;
}

} // namespace rdvc
