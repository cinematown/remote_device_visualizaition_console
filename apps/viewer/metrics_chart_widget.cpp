#include "metrics_chart_widget.hpp"

#include <algorithm>
#include <cstdint>

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPointF>
#include <QRectF>

namespace rdvc {
namespace {

constexpr int kMaxSamples = 60;

std::uint64_t max_msg_rate(const std::deque<ServerMetrics>& samples)
{
    std::uint64_t maximum = 1;
    for (const auto& sample : samples) {
        maximum = std::max(maximum, sample.msg_per_sec);
    }
    return maximum;
}

std::uint64_t max_active(const std::deque<ServerMetrics>& samples)
{
    std::uint64_t maximum = 1;
    for (const auto& sample : samples) {
        maximum = std::max(maximum, sample.active);
    }
    return maximum;
}

template <typename ValueFn>
QPainterPath build_path(
    const std::deque<ServerMetrics>& samples,
    const QRectF& plot_area,
    std::uint64_t maximum,
    ValueFn value_fn)
{
    QPainterPath path;
    if (samples.empty()) {
        return path;
    }

    const auto denominator = std::max<std::size_t>(1, samples.size() - 1);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const double x =
            plot_area.left()
            + (plot_area.width() * static_cast<double>(index) / static_cast<double>(denominator));
        const double value = static_cast<double>(value_fn(samples[index]));
        const double y =
            plot_area.bottom() - (plot_area.height() * value / static_cast<double>(maximum));

        if (index == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }

    return path;
}

} // namespace

MetricsChartWidget::MetricsChartWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(140);
}

void MetricsChartWidget::appendSample(const ServerMetrics& metrics)
{
    samples_.push_back(metrics);
    while (samples_.size() > kMaxSamples) {
        samples_.pop_front();
    }
    update();
}

void MetricsChartWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds = rect();
    painter.fillRect(bounds, QColor(248, 249, 250));

    const QRectF plot_area = bounds.adjusted(44, 34, -18, -30);
    painter.setPen(QPen(QColor(208, 213, 221), 1));
    painter.drawRect(plot_area);

    painter.setFont(QFont("Sans Serif", 9));
    painter.setPen(QColor(32, 33, 36));
    painter.drawText(
        bounds.adjusted(12, 6, -12, -4),
        Qt::AlignTop | Qt::AlignLeft,
        "Server Metrics");

    const QRectF msg_legend(bounds.left() + 138.0, bounds.top() + 8.0, 86.0, 16.0);
    const QRectF active_legend(bounds.left() + 238.0, bounds.top() + 8.0, 116.0, 16.0);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(46, 160, 67));
    painter.drawRect(QRectF(msg_legend.left(), msg_legend.top() + 5.0, 16.0, 3.0));
    painter.setBrush(QColor(9, 105, 218));
    painter.drawRect(QRectF(active_legend.left(), active_legend.top() + 5.0, 16.0, 3.0));

    painter.setPen(QColor(32, 33, 36));
    painter.drawText(msg_legend.adjusted(22, 0, 0, 0), Qt::AlignVCenter, "Msg/s");
    painter.drawText(active_legend.adjusted(22, 0, 0, 0), Qt::AlignVCenter, "Active conns");

    painter.setPen(QColor(95, 99, 104));
    painter.drawText(
        bounds.adjusted(12, 4, -12, -4),
        Qt::AlignBottom | Qt::AlignLeft,
        "last 60 samples");
    painter.drawText(
        bounds.adjusted(12, 4, -12, -4),
        Qt::AlignBottom | Qt::AlignRight,
        "each line uses its own scale");

    if (samples_.empty()) {
        painter.drawText(plot_area, Qt::AlignCenter, "Waiting for metrics");
        return;
    }

    // Each series is scaled to its own recent maximum so low connection counts
    // remain visible next to high message throughput.
    const auto msg_path = build_path(
        samples_, plot_area, max_msg_rate(samples_), [](const ServerMetrics& sample) {
            return sample.msg_per_sec;
        });
    const auto active_path = build_path(
        samples_, plot_area, max_active(samples_), [](const ServerMetrics& sample) {
            return sample.active;
        });

    painter.setPen(QPen(QColor(46, 160, 67), 2));
    painter.drawPath(msg_path);

    painter.setPen(QPen(QColor(9, 105, 218), 2));
    painter.drawPath(active_path);

    const auto& latest = samples_.back();
    painter.setPen(QColor(32, 33, 36));
    painter.drawText(
        bounds.adjusted(12, 4, -12, -4),
        Qt::AlignTop | Qt::AlignRight,
        QString("Msg/s %1   Active %2   ACK %3")
            .arg(latest.msg_per_sec)
            .arg(latest.active)
            .arg(latest.ack_sent));
}

} // namespace rdvc
