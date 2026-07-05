#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "rdvc/protocol/device_status.hpp"

namespace rdvc {

class DeviceStatusStore {
public:
    // Store the latest status per device_id so future viewer/API layers can
    // query current device state without knowing how the server received it.
    void upsert(DeviceStatus status);

    std::optional<DeviceStatus> find(std::string_view device_id) const;
    std::size_t size() const;

private:
    std::unordered_map<std::string, DeviceStatus> statuses_by_id_;
};

} // namespace rdvc
