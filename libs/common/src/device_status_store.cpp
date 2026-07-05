#include "rdvc/common/device_status_store.hpp"

namespace rdvc {

void DeviceStatusStore::upsert(DeviceStatus status)
{
    // device_id is the stable key: repeated reports replace the previous
    // snapshot, while new ids expand the registry.
    statuses_by_id_[status.device_id] = std::move(status);
}

std::optional<DeviceStatus> DeviceStatusStore::find(std::string_view device_id) const
{
    const auto found = statuses_by_id_.find(std::string(device_id));
    if (found == statuses_by_id_.end()) {
        return std::nullopt;
    }

    return found->second;
}


std::size_t DeviceStatusStore::size() const
{
    return statuses_by_id_.size();
}

} // namespace rdvc
