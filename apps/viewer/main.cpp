#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QString>
#include <QTableView>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "fleet_map_widget.hpp"
#include "metrics_chart_widget.hpp"
#include "network_worker.hpp"
#include "rdvc/protocol/server_metrics.hpp"

namespace rdvc {

constexpr auto kServerHost = "127.0.0.1";
constexpr quint16 kServerPort = 5000;
constexpr qint64 kTableStaleMs = 5000;

struct TableDeviceInfo {
    int row = -1;
    DeviceStatus status;
    qint64 last_seen_ms = 0;
};

QString metric_number(std::uint64_t value)
{
    return QString::number(static_cast<qulonglong>(value));
}

void set_metric_label(QLabel& label, const QString& name, const QString& value)
{
    label.setText(QString("%1: %2").arg(name, value));
}

qint64 now_ms()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QString age_text(qint64 age_ms)
{
    if (age_ms < 1000) {
        return "now";
    }

    return QString("%1s").arg(age_ms / 1000);
}

QString display_state(const DeviceStatus& status, qint64 age_ms)
{
    if (age_ms > kTableStaleMs) {
        return "STALE";
    }

    return QString::fromStdString(status.state);
}

void set_model_text(
    QStandardItemModel& model,
    int row,
    int column,
    const QString& text,
    bool stale)
{
    auto* item = model.item(row, column);
    if (item == nullptr) {
        item = new QStandardItem;
        model.setItem(row, column, item);
    }

    if (item->text() != text) {
        item->setText(text);
    }

    item->setForeground(stale ? QBrush(QColor(108, 117, 125)) : QBrush(QColor(32, 33, 36)));
}

void set_device_row(
    QStandardItemModel& model,
    std::unordered_map<std::string, TableDeviceInfo>& table_devices,
    const DeviceStatus& status,
    qint64 seen_ms)
{
    auto& info = table_devices[status.device_id];
    if (info.row < 0) {
        info.row = model.rowCount();
        model.insertRow(info.row);
    }

    info.status = status;
    info.last_seen_ms = seen_ms;

    const bool stale = false;
    set_model_text(model, info.row, 0, QString::fromStdString(status.device_id), stale);
    set_model_text(model, info.row, 1, display_state(status, 0), stale);
    set_model_text(model, info.row, 2, QString::number(status.battery_percent), stale);
    set_model_text(model, info.row, 3, QString::number(status.x, 'f', 1), stale);
    set_model_text(model, info.row, 4, QString::number(status.y, 'f', 1), stale);
    set_model_text(model, info.row, 5, QString::number(status.z, 'f', 1), stale);
    set_model_text(model, info.row, 6, age_text(0), stale);
}

void refresh_table_ages(
    QStandardItemModel& model,
    std::unordered_map<std::string, TableDeviceInfo>& table_devices)
{
    const qint64 current_ms = now_ms();
    for (auto& [device_id, info] : table_devices) {
        (void)device_id;
        const qint64 age_ms = current_ms - info.last_seen_ms;
        const bool stale = age_ms > kTableStaleMs;
        set_model_text(model, info.row, 1, display_state(info.status, age_ms), stale);
        set_model_text(model, info.row, 6, age_text(age_ms), stale);
    }
}

} // namespace rdvc

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    qRegisterMetaType<rdvc::DeviceStatus>("rdvc::DeviceStatus");

    QWidget window;
    window.setWindowTitle("C++ Remote Device Visualization Console");
    window.resize(980, 720);

    auto* status_label = new QLabel("Disconnected");
    auto* connect_button = new QPushButton("Connect");

    auto* model = new QStandardItemModel(&window);
    model->setHorizontalHeaderLabels({"Device ID", "State", "Battery", "X", "Y", "Z", "Age"});

    auto* table = new QTableView;
    table->setModel(model);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* network_thread = new QThread(&window);
    auto* network_worker = new rdvc::NetworkWorker;
    network_worker->moveToThread(network_thread);

    auto* fleet_map = new rdvc::FleetMapWidget;
    auto* metrics_chart = new rdvc::MetricsChartWidget;

    auto* active_metric = new QLabel("Active: 0");
    auto* msg_rate_metric = new QLabel("Msg/s: 0");
    auto* devices_metric = new QLabel("Devices: 0");
    auto* received_metric = new QLabel("Received: 0");
    auto* ack_metric = new QLabel("ACK: 0");
    auto* errors_metric = new QLabel("Errors: 0");

    auto* metrics_grid = new QGridLayout;
    metrics_grid->addWidget(active_metric, 0, 0);
    metrics_grid->addWidget(msg_rate_metric, 0, 1);
    metrics_grid->addWidget(devices_metric, 0, 2);
    metrics_grid->addWidget(received_metric, 1, 0);
    metrics_grid->addWidget(ack_metric, 1, 1);
    metrics_grid->addWidget(errors_metric, 1, 2);

    auto* top_bar = new QHBoxLayout;
    top_bar->addWidget(connect_button);
    top_bar->addWidget(status_label);
    top_bar->addStretch();

    auto* main_splitter = new QSplitter(Qt::Vertical);
    main_splitter->addWidget(fleet_map);
    main_splitter->addWidget(table);
    main_splitter->setStretchFactor(0, 3);
    main_splitter->setStretchFactor(1, 1);

    auto* root_layout = new QVBoxLayout;
    root_layout->addLayout(top_bar);
    root_layout->addLayout(metrics_grid);
    root_layout->addWidget(metrics_chart);
    root_layout->addWidget(main_splitter);
    window.setLayout(root_layout);

    bool viewer_connected = false;
    std::unordered_map<std::string, rdvc::DeviceStatus> pending_statuses;
    std::unordered_map<std::string, rdvc::TableDeviceInfo> table_devices;

    auto* device_flush_timer = new QTimer(&window);
    device_flush_timer->setInterval(100);
    QObject::connect(device_flush_timer, &QTimer::timeout, [&]() {
        if (pending_statuses.empty()) {
            return;
        }

        const qint64 seen_ms = rdvc::now_ms();
        std::vector<rdvc::DeviceStatus> statuses;
        statuses.reserve(pending_statuses.size());

        for (const auto& [device_id, status] : pending_statuses) {
            (void)device_id;
            rdvc::set_device_row(*model, table_devices, status, seen_ms);
            statuses.push_back(status);
        }

        pending_statuses.clear();
        fleet_map->upsertDevices(statuses);
    });
    device_flush_timer->start();

    auto* table_age_timer = new QTimer(&window);
    table_age_timer->setInterval(1000);
    QObject::connect(table_age_timer, &QTimer::timeout, [&]() {
        rdvc::refresh_table_ages(*model, table_devices);
    });
    table_age_timer->start();

    QObject::connect(connect_button, &QPushButton::clicked, [&]() {
        // UI code now requests actions through queued signal/slot calls. The
        // worker owns QTcpSocket, keeping network events away from this thread.
        if (viewer_connected) {
            QMetaObject::invokeMethod(network_worker, "disconnectFromServer");
            return;
        }

        status_label->setText("Connecting...");
        QMetaObject::invokeMethod(
            network_worker,
            "connectToServer",
            Q_ARG(QString, QString::fromUtf8(rdvc::kServerHost)),
            Q_ARG(quint16, rdvc::kServerPort));
    });

    QObject::connect(network_worker, &rdvc::NetworkWorker::connected, [&](const QString&) {
        viewer_connected = true;
        connect_button->setText("Disconnect");
    });

    QObject::connect(network_worker, &rdvc::NetworkWorker::disconnected, [&]() {
        viewer_connected = false;
        connect_button->setText("Connect");
    });

    QObject::connect(
        network_worker,
        &rdvc::NetworkWorker::statusMessageChanged,
        status_label,
        &QLabel::setText);

    QObject::connect(network_worker, &rdvc::NetworkWorker::errorOccurred, [&](const QString& message) {
        status_label->setText(QString("Error: %1").arg(message));
    });

    QObject::connect(network_worker, &rdvc::NetworkWorker::statusReceived, [&](const rdvc::DeviceStatus& status) {
        pending_statuses[status.device_id] = status;
    });

    QObject::connect(network_worker, &rdvc::NetworkWorker::metricsReceived, [&](const QString& metrics_line) {
        const auto metrics = rdvc::parse_metrics_line(metrics_line.toStdString());
        if (!metrics.has_value()) {
            return;
        }

        rdvc::set_metric_label(*active_metric, "Active", rdvc::metric_number(metrics->active));
        rdvc::set_metric_label(*msg_rate_metric, "Msg/s", rdvc::metric_number(metrics->msg_per_sec));
        rdvc::set_metric_label(*devices_metric, "Devices", rdvc::metric_number(metrics->devices));
        rdvc::set_metric_label(*received_metric, "Received", rdvc::metric_number(metrics->received));
        rdvc::set_metric_label(*ack_metric, "ACK", rdvc::metric_number(metrics->ack_sent));

        rdvc::set_metric_label(
            *errors_metric,
            "Errors",
            rdvc::metric_number(metrics->parse_errors + metrics->broadcast_errors));
        metrics_chart->appendSample(*metrics);
    });

    QObject::connect(network_thread, &QThread::finished, network_worker, &QObject::deleteLater);
    QObject::connect(&app, &QApplication::aboutToQuit, [&]() {
        // Shutdown is explicit: ask the worker socket to close, then stop the
        // thread before QApplication destroys UI-owned objects.
        QMetaObject::invokeMethod(network_worker, "disconnectFromServer");
        network_thread->quit();
        network_thread->wait();
    });

    network_thread->start();
    window.show();
    return app.exec();
}
