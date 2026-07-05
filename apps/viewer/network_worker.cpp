#include "network_worker.hpp"

#include <QAbstractSocket>

#include "rdvc/protocol/status_parser.hpp"

namespace rdvc {

NetworkWorker::NetworkWorker(QObject* parent)
    : QObject(parent)
{
}

void NetworkWorker::connectToServer(const QString& host, quint16 port)
{
    ensure_socket();

    if (socket_->state() == QAbstractSocket::ConnectedState
        || socket_->state() == QAbstractSocket::ConnectingState) {
        return;
    }

    // The socket lives in the worker thread, so connect/disconnect and
    // readyRead handling cannot block the Qt UI event loop.
    emit statusMessageChanged("Connecting...");
    socket_->connectToHost(host, port);
}

void NetworkWorker::disconnectFromServer()
{
    if (socket_ == nullptr) {
        emit disconnected();
        return;
    }

    socket_->disconnectFromHost();
}

void NetworkWorker::ensure_socket()
{
    if (socket_ != nullptr) {
        return;
    }

    socket_ = new QTcpSocket(this);

    connect(socket_, &QTcpSocket::connected, this, [this]() {
        const auto endpoint =
            QString("%1:%2").arg(socket_->peerAddress().toString()).arg(socket_->peerPort());
        emit connected(endpoint);
        emit statusMessageChanged(QString("Connected to %1").arg(endpoint));
        socket_->write("HELLO role=viewer\n");
    });

    connect(socket_, &QTcpSocket::disconnected, this, [this]() {
        receive_buffer_.clear();
        emit disconnected();
        emit statusMessageChanged("Disconnected");
    });

    connect(socket_, &QTcpSocket::stateChanged, this, [this](QAbstractSocket::SocketState state) {
        if (state == QAbstractSocket::UnconnectedState
            || state == QAbstractSocket::ConnectedState) {
            return;
        }
        emit statusMessageChanged(QString("Socket state: %1").arg(static_cast<int>(state)));
    });

    connect(socket_, &QTcpSocket::errorOccurred, this, [this]() {
        emit errorOccurred(socket_->errorString());
    });

    connect(socket_, &QTcpSocket::readyRead, this, [this]() {
        receive_buffer_ += socket_->readAll().toStdString();
        consume_status_lines();
    });
}

void NetworkWorker::consume_status_lines()
{
    // TCP can split or merge messages. The worker keeps a byte buffer and
    // emits only complete, parsed DeviceStatus objects to the UI thread.
    while (true) {
        const auto newline = receive_buffer_.find('\n');
        if (newline == std::string::npos) {
            return;
        }

        const auto line = receive_buffer_.substr(0, newline + 1);
        receive_buffer_.erase(0, newline + 1);

        if (line.starts_with("METRICS ")) {
            emit metricsReceived(QString::fromStdString(line));
            continue;
        }

        if (const auto status = parse_status_line(line)) {
            emit statusReceived(*status);
        }
    }
}

} // namespace rdvc
