#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rdvc {

struct ServerMetrics {
    std::uint64_t uptime_sec = 0;
    std::uint64_t active = 0;
    std::uint64_t accepted = 0;
    std::uint64_t disconnected = 0;
    std::uint64_t devices = 0;
    std::uint64_t received = 0;
    std::uint64_t ack_sent = 0;
    std::uint64_t parse_errors = 0;
    std::uint64_t broadcast_errors = 0;
    std::uint64_t msg_per_sec = 0;
};

std::string format_metrics_line(const ServerMetrics& metrics);
std::optional<ServerMetrics> parse_metrics_line(std::string_view line);

} // namespace rdvc
