#pragma once

#include <string>
#include <unordered_map>

#include <QOpenGLWidget>

#include "rdvc/protocol/device_status.hpp"

namespace rdvc {

class DeviceViewportWidget : public QOpenGLWidget {
public:
    explicit DeviceViewportWidget(QWidget* parent = nullptr);

    void upsertDevice(const DeviceStatus& status);

protected:
    void paintGL() override;

private:
    std::unordered_map<std::string, DeviceStatus> devices_by_id_;
};

} // namespace rdvc
