#include "fleet_map_widget.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QRectF>
#include <QToolTip>
#include <QWheelEvent>

namespace rdvc {
namespace {

constexpr double kMinimumWorldSpan = 10.0;
constexpr double kMarkerHitRadius = 10.0;
constexpr qint64 kFreshMs = 1200;
constexpr qint64 kStaleMs = 5000;

qint64 now_ms()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QColor color_for_state(const std::string& state, qint64 age_ms)
{
    QColor color = state == "OK" ? QColor(46, 160, 67) : QColor(210, 68, 58);
    if (age_ms > kStaleMs) {
        color = QColor(108, 117, 125);
    }

    if (age_ms > kStaleMs) {
        color.setAlpha(90);
    } else if (age_ms > kFreshMs) {
        color.setAlpha(170);
    }

    return color;
}

} // namespace

FleetMapWidget::FleetMapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(320);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void FleetMapWidget::upsertDevice(const DeviceStatus& status)
{
    devices_by_id_[status.device_id] = DeviceSnapshot{status, now_ms()};
    if (auto_fit_enabled_) {
        updateAutoFit();
    }
    update();
}

void FleetMapWidget::autoFit()
{
    auto_fit_enabled_ = true;
    updateAutoFit();
    update();
}

QRectF FleetMapWidget::plotArea() const
{
    return rect().adjusted(16, 34, -16, -18);
}

QRectF FleetMapWidget::worldBounds() const
{
    if (devices_by_id_.empty()) {
        return QRectF(-10.0, -10.0, 20.0, 20.0);
    }

    double min_x = std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double max_y = std::numeric_limits<double>::lowest();

    for (const auto& [device_id, snapshot] : devices_by_id_) {
        (void)device_id;
        min_x = std::min(min_x, snapshot.status.x);
        min_y = std::min(min_y, snapshot.status.y);
        max_x = std::max(max_x, snapshot.status.x);
        max_y = std::max(max_y, snapshot.status.y);
    }

    const double span_x = std::max(kMinimumWorldSpan, max_x - min_x);
    const double span_y = std::max(kMinimumWorldSpan, max_y - min_y);
    const double pad_x = span_x * 0.12;
    const double pad_y = span_y * 0.12;
    return QRectF(
        min_x - pad_x,
        min_y - pad_y,
        span_x + pad_x * 2.0,
        span_y + pad_y * 2.0);
}

QPointF FleetMapWidget::worldToScreen(double x, double y) const
{
    return {x * scale_ + pan_.x(), -y * scale_ + pan_.y()};
}

QPointF FleetMapWidget::screenToWorld(const QPointF& point) const
{
    return {(point.x() - pan_.x()) / scale_, -(point.y() - pan_.y()) / scale_};
}

void FleetMapWidget::updateAutoFit()
{
    const QRectF area = plotArea();
    if (area.width() <= 1.0 || area.height() <= 1.0) {
        return;
    }

    const QRectF bounds = worldBounds();
    const double next_scale = std::min(
        area.width() / std::max(1.0, bounds.width()),
        area.height() / std::max(1.0, bounds.height()));
    scale_ = std::clamp(next_scale, 2.0, 80.0);

    const QPointF world_center = bounds.center();
    const QPointF screen_center = area.center();
    pan_ = {
        screen_center.x() - world_center.x() * scale_,
        screen_center.y() + world_center.y() * scale_};
}

std::optional<std::string> FleetMapWidget::nearestDeviceId(const QPointF& point) const
{
    std::optional<std::string> nearest_id;
    double nearest_distance = kMarkerHitRadius;

    for (const auto& [device_id, snapshot] : devices_by_id_) {
        const QPointF marker = worldToScreen(snapshot.status.x, snapshot.status.y);
        const double distance =
            std::hypot(marker.x() - point.x(), marker.y() - point.y());
        if (distance <= nearest_distance) {
            nearest_distance = distance;
            nearest_id = device_id;
        }
    }

    return nearest_id;
}

void FleetMapWidget::updateHover(const QPointF& point)
{
    hovered_device_id_ = nearestDeviceId(point);
    if (!hovered_device_id_.has_value()) {
        setToolTip(QString{});
        return;
    }

    const auto found = devices_by_id_.find(*hovered_device_id_);
    if (found == devices_by_id_.end()) {
        return;
    }

    const auto& status = found->second.status;
    setToolTip(QString("%1  %2  battery %3%  z %4")
                   .arg(QString::fromStdString(status.device_id))
                   .arg(QString::fromStdString(status.state))
                   .arg(status.battery_percent)
                   .arg(status.z, 0, 'f', 1));
}

void FleetMapWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds = rect();
    const QRectF area = plotArea();
    painter.fillRect(bounds, QColor(247, 248, 250));

    painter.setPen(QPen(QColor(220, 225, 232), 1));
    painter.drawRect(area);

    painter.setPen(QPen(QColor(232, 236, 242), 1));
    for (int index = 1; index < 10; ++index) {
        const double x = area.left() + area.width() * index / 10.0;
        const double y = area.top() + area.height() * index / 10.0;
        painter.drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
    }

    painter.setFont(QFont("Sans Serif", 9));
    painter.setPen(QColor(32, 33, 36));
    painter.drawText(
        bounds.adjusted(16, 8, -16, -8),
        Qt::AlignTop | Qt::AlignLeft,
        QString("Fleet Map  devices %1").arg(devices_by_id_.size()));

    painter.setPen(QColor(95, 99, 104));
    painter.drawText(
        bounds.adjusted(16, 8, -16, -8),
        Qt::AlignTop | Qt::AlignRight,
        QString("scale %1x").arg(scale_, 0, 'f', 1));

    if (devices_by_id_.empty()) {
        painter.setPen(QColor(95, 99, 104));
        painter.drawText(area, Qt::AlignCenter, "Waiting for device status");
        return;
    }

    const qint64 current_ms = now_ms();
    const double marker_radius = devices_by_id_.size() > 250 ? 3.5 : 5.0;
    std::vector<const DeviceSnapshot*> highlighted;

    for (const auto& [device_id, snapshot] : devices_by_id_) {
        const QPointF point = worldToScreen(snapshot.status.x, snapshot.status.y);
        if (!area.adjusted(-12, -12, 12, 12).contains(point)) {
            continue;
        }

        const qint64 age_ms = current_ms - snapshot.last_seen_ms;
        const QColor fill = color_for_state(snapshot.status.state, age_ms);
        const bool is_hovered = hovered_device_id_.has_value() && *hovered_device_id_ == device_id;
        const bool is_selected =
            selected_device_id_.has_value() && *selected_device_id_ == device_id;

        if (age_ms < kFreshMs) {
            const double ring_radius = marker_radius + 4.0
                + 4.0 * (1.0 - static_cast<double>(age_ms) / static_cast<double>(kFreshMs));
            QColor ring = fill;
            ring.setAlpha(60);
            painter.setPen(QPen(ring, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(point, ring_radius, ring_radius);
        }

        painter.setPen(QPen(QColor(32, 33, 36), is_selected ? 2 : 1));
        painter.setBrush(fill);
        painter.drawEllipse(point, marker_radius, marker_radius);

        if (is_hovered || is_selected) {
            highlighted.push_back(&snapshot);
        }
    }

    painter.setFont(QFont("Sans Serif", 9));
    for (const auto* snapshot : highlighted) {
        const auto& status = snapshot->status;
        const QPointF point = worldToScreen(status.x, status.y);
        const QString label = QString::fromStdString(status.device_id);
        const QRectF label_rect(point.x() + 9.0, point.y() - 20.0, 110.0, 18.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, 230));
        painter.drawRoundedRect(label_rect, 4.0, 4.0);
        painter.setPen(QColor(32, 33, 36));
        painter.drawText(label_rect.adjusted(5, 0, -5, 0), Qt::AlignVCenter, label);
    }
}

void FleetMapWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        selected_device_id_ = nearestDeviceId(event->position());
        dragging_ = !selected_device_id_.has_value();
        last_mouse_position_ = event->pos();
        update();
    }
}

void FleetMapWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (dragging_) {
        auto_fit_enabled_ = false;
        const QPoint delta = event->pos() - last_mouse_position_;
        pan_ += QPointF(delta);
        last_mouse_position_ = event->pos();
        update();
        return;
    }

    updateHover(event->position());
    update();
}

void FleetMapWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        dragging_ = false;
    }
}

void FleetMapWidget::mouseDoubleClickEvent(QMouseEvent*)
{
    autoFit();
}

void FleetMapWidget::wheelEvent(QWheelEvent* event)
{
    auto_fit_enabled_ = false;
    const QPointF cursor = event->position();
    const QPointF before = screenToWorld(cursor);
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    scale_ = std::clamp(scale_ * factor, 2.0, 140.0);
    pan_ = {cursor.x() - before.x() * scale_, cursor.y() + before.y() * scale_};
    updateHover(cursor);
    update();
}

} // namespace rdvc
