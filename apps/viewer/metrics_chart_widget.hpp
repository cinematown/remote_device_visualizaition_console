#pragma once

#include <deque>

#include <QWidget>

#include "rdvc/protocol/server_metrics.hpp"

namespace rdvc {

class MetricsChartWidget : public QWidget {
public:
    explicit MetricsChartWidget(QWidget* parent = nullptr);

    void appendSample(const ServerMetrics& metrics);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::deque<ServerMetrics> samples_;
};

} // namespace rdvc
