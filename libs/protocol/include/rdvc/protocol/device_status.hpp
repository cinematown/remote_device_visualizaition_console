#pragma once

#include <cstdint>
#include <string>

namespace rdvc {

struct DeviceStatus {
    std::string device_id;
    std::string state;
    int battery_percent = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    std::uint64_t sequence = 0;
    std::uint64_t sent_ms = 0;
    bool ack_requested = false;
};

} // namespace rdvc
