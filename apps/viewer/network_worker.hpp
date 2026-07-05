#pragma once

#include <string>

#include <QObject>
#include <QString>
#include <QTcpSocket>

#include "rdvc/protocol/device_status.hpp"

Q_DECLARE_METATYPE(rdvc::DeviceStatus)

namespace rdvc {

class NetworkWorker : public QObject {
    Q_OBJECT

public:
    explicit NetworkWorker(QObject* parent = nullptr);

public slots:
    void connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();

signals:
    void connected(const QString& endpoint);
    void disconnected();
    void statusMessageChanged(const QString& message);
    void statusReceived(const rdvc::DeviceStatus& status);
    void errorOccurred(const QString& message);

private:
    void ensure_socket();
    void consume_status_lines();

    QTcpSocket* socket_ = nullptr;
    std::string receive_buffer_;
};

} // namespace rdvc
