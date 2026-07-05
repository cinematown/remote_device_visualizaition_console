#include <QApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QString>
#include <QTableView>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include "device_viewport_widget.hpp"
#include "network_worker.hpp"

namespace rdvc {

constexpr auto kServerHost = "127.0.0.1";
constexpr quint16 kServerPort = 5000;

void set_device_row(QStandardItemModel& model, const DeviceStatus& status)
{
    // The table is keyed by device_id so later updates replace the existing
    // row instead of appending duplicate devices.
    int row = -1;
    for (int index = 0; index < model.rowCount(); ++index) {
        const auto* id_item = model.item(index, 0);
        if (id_item != nullptr && id_item->text().toStdString() == status.device_id) {
            row = index;
            break;
        }
    }

    if (row < 0) {
        row = model.rowCount();
        model.insertRow(row);
    }

    model.setItem(row, 0, new QStandardItem(QString::fromStdString(status.device_id)));
    model.setItem(row, 1, new QStandardItem(QString::fromStdString(status.state)));
    model.setItem(row, 2, new QStandardItem(QString::number(status.battery_percent)));
    model.setItem(row, 3, new QStandardItem(QString::number(status.x, 'f', 1)));
    model.setItem(row, 4, new QStandardItem(QString::number(status.y, 'f', 1)));
    model.setItem(row, 5, new QStandardItem(QString::number(status.z, 'f', 1)));
}

} // namespace rdvc

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    qRegisterMetaType<rdvc::DeviceStatus>("rdvc::DeviceStatus");

    QWidget window;
    window.setWindowTitle("C++ Remote Device Visualization Console");
    window.resize(760, 420);

    auto* status_label = new QLabel("Disconnected");
    auto* connect_button = new QPushButton("Connect");

    auto* model = new QStandardItemModel(&window);
    model->setHorizontalHeaderLabels({"Device ID", "State", "Battery", "X", "Y", "Z"});

    auto* table = new QTableView;
    table->setModel(model);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    auto* network_thread = new QThread(&window);
    auto* network_worker = new rdvc::NetworkWorker;
    network_worker->moveToThread(network_thread);

    auto* viewport = new rdvc::DeviceViewportWidget;

    auto* top_bar = new QHBoxLayout;
    top_bar->addWidget(connect_button);
    top_bar->addWidget(status_label);
    top_bar->addStretch();

    auto* root_layout = new QVBoxLayout;
    root_layout->addLayout(top_bar);
    root_layout->addWidget(table);
    root_layout->addWidget(viewport);
    window.setLayout(root_layout);

    bool viewer_connected = false;

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
        rdvc::set_device_row(*model, status);
        viewport->upsertDevice(status);
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
