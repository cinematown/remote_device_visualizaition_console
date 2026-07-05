#include "device_viewport_widget.hpp"

#include <algorithm>

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPointF>
#include <QRectF>

namespace rdvc {
namespace {

QPointF project_point(double x, double y, double z, const QRectF& viewport)
{
    // A lightweight isometric projection is enough for Phase 8: it makes x/y/z
    // visible in a QOpenGLWidget without introducing shader or camera code yet.
    constexpr double scale = 5.0;
    const QPointF center = viewport.center();
    const double projected_x = (x - y) * scale;
    const double projected_y = ((x + y) * 0.45 - z) * scale;
    return {center.x() + projected_x, center.y() - projected_y};
}

QColor color_for_state(const std::string& state)
{
    if (state == "OK") {
        return QColor(46, 160, 67);
    }
    return QColor(210, 68, 58);
}

} // namespace

DeviceViewportWidget::DeviceViewportWidget(QWidget* parent)
    : QOpenGLWidget(parent)
{
    setMinimumHeight(260);
}

void DeviceViewportWidget::upsertDevice(const DeviceStatus& status)
{
    devices_by_id_[status.device_id] = status;
    update();
}

void DeviceViewportWidget::paintGL()
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF area = rect().adjusted(16, 16, -16, -16);
    painter.fillRect(rect(), QColor(248, 249, 250));

    const auto origin = project_point(0.0, 0.0, 0.0, area);
    const auto x_axis = project_point(35.0, 0.0, 0.0, area);
    const auto y_axis = project_point(0.0, 35.0, 0.0, area);
    const auto z_axis = project_point(0.0, 0.0, 35.0, area);

    // Draw simple coordinate axes first so device positions have spatial
    // context even before the later VTK/model-viewer phases.
    painter.setPen(QPen(QColor(210, 68, 58), 2));
    painter.drawLine(origin, x_axis);
    painter.drawText(x_axis + QPointF(6, 0), "X");

    painter.setPen(QPen(QColor(46, 160, 67), 2));
    painter.drawLine(origin, y_axis);
    painter.drawText(y_axis + QPointF(6, 0), "Y");

    painter.setPen(QPen(QColor(9, 105, 218), 2));
    painter.drawLine(origin, z_axis);
    painter.drawText(z_axis + QPointF(6, 0), "Z");

    painter.setFont(QFont("Sans Serif", 9));
    for (const auto& [device_id, status] : devices_by_id_) {
        const auto point = project_point(status.x, status.y, status.z, area);
        const QRectF marker(point.x() - 5.0, point.y() - 5.0, 10.0, 10.0);

        painter.setPen(QPen(QColor(32, 33, 36), 1));
        painter.setBrush(color_for_state(status.state));
        painter.drawRect(marker);
        painter.drawText(point + QPointF(8, -8), QString::fromStdString(device_id));
    }

    if (devices_by_id_.empty()) {
        painter.setPen(QColor(95, 99, 104));
        painter.drawText(area, Qt::AlignCenter, "No device positions yet");
    }
}

} // namespace rdvc
