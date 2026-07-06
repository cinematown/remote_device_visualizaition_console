#include "rdvc/protocol/server_metrics.hpp"

#include <charconv>

namespace rdvc {
namespace {

constexpr std::string_view kMetricsPrefix = "METRICS";

bool parse_u64(std::string_view value, std::uint64_t& output)
{
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto result = std::from_chars(first, last, output);
    return result.ec == std::errc{} && result.ptr == last;
}

bool assign_field(ServerMetrics& metrics, std::string_view key, std::string_view value)
{
    if (key == "uptime_sec") {
        return parse_u64(value, metrics.uptime_sec);
    }

    if (key == "active") {
        return parse_u64(value, metrics.active);
    }

    if (key == "accepted") {
        return parse_u64(value, metrics.accepted);
    }

    if (key == "disconnected") {
        return parse_u64(value, metrics.disconnected);
    }

    if (key == "devices") {
        return parse_u64(value, metrics.devices);
    }

    if (key == "received") {
        return parse_u64(value, metrics.received);
    }

    if (key == "ack_sent") {
        return parse_u64(value, metrics.ack_sent);
    }

    if (key == "parse_errors") {
        return parse_u64(value, metrics.parse_errors);
    }

    if (key == "broadcast_errors") {
        return parse_u64(value, metrics.broadcast_errors);
    }

    if (key == "msg_per_sec") {
        return parse_u64(value, metrics.msg_per_sec);
    }

    return true;
}

} // namespace

std::string format_metrics_line(const ServerMetrics& metrics)
{
    std::string line = "METRICS uptime_sec=";
    line += std::to_string(metrics.uptime_sec);
    line += " active=";
    line += std::to_string(metrics.active);
    line += " accepted=";
    line += std::to_string(metrics.accepted);
    line += " disconnected=";
    line += std::to_string(metrics.disconnected);
    line += " devices=";
    line += std::to_string(metrics.devices);
    line += " received=";
    line += std::to_string(metrics.received);
    line += " ack_sent=";
    line += std::to_string(metrics.ack_sent);
    line += " parse_errors=";
    line += std::to_string(metrics.parse_errors);
    line += " broadcast_errors=";
    line += std::to_string(metrics.broadcast_errors);
    line += " msg_per_sec=";
    line += std::to_string(metrics.msg_per_sec);
    line += '\n';
    return line;
}

std::optional<ServerMetrics> parse_metrics_line(std::string_view line)
{
    if (!line.empty() && line.back() == '\n') {
        line.remove_suffix(1);
    }

    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }

    if (!line.starts_with(kMetricsPrefix)) {
        return std::nullopt;
    }

    if (line.size() > kMetricsPrefix.size() && line[kMetricsPrefix.size()] != ' ') {
        return std::nullopt;
    }

    ServerMetrics metrics{};
    std::size_t position = kMetricsPrefix.size();

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

        if (!assign_field(metrics, token.substr(0, equals), token.substr(equals + 1))) {
            return std::nullopt;
        }

        position = token_end;
    }

    return metrics;
}

} // namespace rdvc
