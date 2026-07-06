#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include <QPoint>
#include <QPointF>
#include <QWidget>

#include "rdvc/protocol/device_status.hpp"

namespace rdvc {

class FleetMapWidget : public QWidget {
public:
    explicit FleetMapWidget(QWidget* parent = nullptr);

    void upsertDevice(const DeviceStatus& status);
    void autoFit();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct DeviceSnapshot {
        DeviceStatus status;
        qint64 last_seen_ms = 0;
    };

    QRectF plotArea() const;
    QRectF worldBounds() const;
    QPointF worldToScreen(double x, double y) const;
    QPointF screenToWorld(const QPointF& point) const;
    void updateAutoFit();
    void updateHover(const QPointF& point);
    std::optional<std::string> nearestDeviceId(const QPointF& point) const;

    std::unordered_map<std::string, DeviceSnapshot> devices_by_id_;
    std::optional<std::string> hovered_device_id_;
    std::optional<std::string> selected_device_id_;
    QPoint last_mouse_position_;
    QPointF pan_{0.0, 0.0};
    double scale_ = 1.0;
    bool auto_fit_enabled_ = true;
    bool dragging_ = false;
};

} // namespace rdvc
