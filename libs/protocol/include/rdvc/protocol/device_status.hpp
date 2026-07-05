#pragma once

#include <string>

namespace rdvc {

struct DeviceStatus {
    std::string device_id;
    std::string state;
    int battery_percent = 0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

} // namespace rdvc
