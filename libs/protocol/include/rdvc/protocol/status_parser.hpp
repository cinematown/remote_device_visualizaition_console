#pragma once

#include <optional>
#include <string_view>

#include "rdvc/protocol/device_status.hpp"

namespace rdvc {

std::optional<DeviceStatus> parse_status_line(std::string_view line);

} // namespace rdvc
