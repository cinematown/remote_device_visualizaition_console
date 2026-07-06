#include <array>
#include <chrono>
#include <cstdint>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "mqtt_bridge.hpp"
#include "rdvc/common/device_status_store.hpp"
#include "rdvc/protocol/server_metrics.hpp"
#include "rdvc/protocol/status_parser.hpp"

namespace rdvc {

constexpr std::string_view kListenAddress = "127.0.0.1";
constexpr int kListenPort = 5000;
constexpr std::string_view kMqttHost = "127.0.0.1";
constexpr int kMqttPort = 1883;
constexpr int kMaxEvents = 128;
constexpr int kListenBacklog = 1024;

volatile std::sig_atomic_t g_should_stop = 0;

void handle_signal(int)
{
    g_should_stop = 1;
}

class Socket {
public:
    explicit Socket(int fd)
        : fd_(fd)
    {
        if (fd_ < 0) {
            throw std::runtime_error(std::strerror(errno));
        }
    }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept
        : fd_(other.fd_)
    {
        other.fd_ = -1;
    }

    Socket& operator=(Socket&& other) noexcept
    {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    ~Socket()
    {
        close();
    }

    int fd() const
    {
        return fd_;
    }

private:
    void close()
    {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    int fd_;
};

class Epoll {
public:
    Epoll()
        : fd_(::epoll_create1(EPOLL_CLOEXEC))
    {
        if (fd_ < 0) {
            throw std::runtime_error(std::strerror(errno));
        }
    }

    Epoll(const Epoll&) = delete;
    Epoll& operator=(const Epoll&) = delete;

    ~Epoll()
    {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    int fd() const
    {
        return fd_;
    }

private:
    int fd_;
};

void set_non_blocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        throw std::runtime_error(std::strerror(errno));
    }

    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error(std::strerror(errno));
    }
}

void add_epoll_fd(int epoll_fd, int fd)
{
    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = fd;

    if (::epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
        throw std::runtime_error(std::strerror(errno));
    }
}

void remove_epoll_fd(int epoll_fd, int fd)
{
    if (::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        std::cerr << "epoll remove error for fd " << fd << ": "
                  << std::strerror(errno) << '\n';
    }
    ::close(fd);
}

Socket create_listening_socket()
{
    Socket listen_socket{::socket(AF_INET, SOCK_STREAM, 0)};
    set_non_blocking(listen_socket.fd());

    int reuse = 1;
    if (::setsockopt(
            listen_socket.fd(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))
        < 0) {
        throw std::runtime_error(std::strerror(errno));
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kListenPort);
    if (::inet_pton(AF_INET, kListenAddress.data(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid listen address");
    }

    if (::bind(
            listen_socket.fd(),
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address))
        < 0) {
        throw std::runtime_error(std::strerror(errno));
    }

    if (::listen(listen_socket.fd(), kListenBacklog) < 0) {
        throw std::runtime_error(std::strerror(errno));
    }

    return listen_socket;
}

struct ServerCounters {
    std::uint64_t start_ms = 0;
    std::uint64_t last_report_ms = 0;
    std::uint64_t last_report_status_received = 0;
    std::uint64_t total_accepted = 0;
    std::uint64_t total_disconnected = 0;
    std::uint64_t status_received = 0;
    std::uint64_t ack_sent = 0;
    std::uint64_t parse_errors = 0;
    std::uint64_t broadcast_errors = 0;
};

void accept_ready_clients(
    int epoll_fd,
    int listen_fd,
    std::unordered_map<int, std::string>& client_buffers,
    std::unordered_set<int>& telemetry_client_fds,
    ServerCounters& counters)
{
    while (true) {
        sockaddr_in client_address{};
        socklen_t client_address_size = sizeof(client_address);
        const int client_fd = ::accept(
            listen_fd,
            reinterpret_cast<sockaddr*>(&client_address),
            &client_address_size);

        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            throw std::runtime_error(std::strerror(errno));
        }

        set_non_blocking(client_fd);
        add_epoll_fd(epoll_fd, client_fd);
        client_buffers.emplace(client_fd, std::string{});
        telemetry_client_fds.insert(client_fd);
        ++counters.total_accepted;

        std::array<char, INET_ADDRSTRLEN> client_ip{};
        const char* ip_text = ::inet_ntop(
            AF_INET, &client_address.sin_addr, client_ip.data(), client_ip.size());
        std::cout << "Client connected: fd=" << client_fd << " from "
                  << (ip_text == nullptr ? "unknown" : ip_text) << ':'
                  << ntohs(client_address.sin_port) << '\n';
    }
}

bool receive_available_data(int client_fd, std::string& input_buffer)
{
    std::array<char, 1024> buffer{};

    while (true) {
        const ssize_t received = ::recv(client_fd, buffer.data(), buffer.size(), 0);
        if (received > 0) {
            input_buffer.append(buffer.data(), static_cast<std::size_t>(received));
            continue;
        }

        if (received == 0) {
            return false;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }

        throw std::runtime_error(std::strerror(errno));
    }
}

std::optional<std::string> take_next_line(std::string& input_buffer)
{
    const auto newline = input_buffer.find('\n');
    if (newline == std::string::npos) {
        return std::nullopt;
    }

    auto line = input_buffer.substr(0, newline + 1);
    input_buffer.erase(0, newline + 1);
    return line;
}

std::uint64_t now_ms()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

bool send_ack(int client_fd, const DeviceStatus& status)
{
    // ACK is the minimal timing hook for the upcoming load generator: each
    // client can measure send-to-ACK round trip latency per sequence number.
    std::string ack = "ACK device_id=";
    ack += status.device_id;
    ack += " seq=";
    ack += std::to_string(status.sequence);
    ack += " server_ms=";
    ack += std::to_string(now_ms());
    ack += '\n';

    const ssize_t sent = ::send(client_fd, ack.data(), ack.size(), MSG_NOSIGNAL);
    if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        std::cerr << "ack send error for fd " << client_fd << ": "
                  << std::strerror(errno) << '\n';
        return false;
    }

    return sent >= 0;
}

std::optional<DeviceStatus> handle_status_line(
    std::string_view line,
    DeviceStatusStore& status_store,
    ServerCounters& counters)
{
    const auto status = parse_status_line(line);
    if (!status.has_value()) {
        ++counters.parse_errors;
        std::cerr << "Invalid STATUS line: " << line;
        return std::nullopt;
    }

    ++counters.status_received;

    // Keep only the latest status per device. This is the in-memory source of
    // truth that later phases expose to the Qt viewer and MQTT bridge.
    status_store.upsert(*status);

    if (!status->ack_requested) {
        std::cout << "Received: " << line;
        std::cout << "Parsed device status: id=" << status->device_id
                  << " state=" << status->state
                  << " battery=" << status->battery_percent << " position=("
                  << status->x << ", " << status->y << ", " << status->z << ")\n";
        std::cout << "Stored devices: " << status_store.size() << '\n';
    }

    return status;
}

bool send_line(int client_fd, std::string_view line, ServerCounters& counters)
{
    const ssize_t sent = ::send(client_fd, line.data(), line.size(), MSG_NOSIGNAL);
    if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        ++counters.broadcast_errors;
        std::cerr << "broadcast error for fd " << client_fd << ": "
                  << std::strerror(errno) << '\n';
        return false;
    }

    return sent >= 0;
}

void broadcast_to_viewers(
    const std::unordered_set<int>& viewer_client_fds,
    int source_fd,
    std::string_view line,
    ServerCounters& counters)
{
    // Only viewer clients receive STATUS/METRICS broadcasts. Load generator
    // sockets must see only ACK lines so latency measurements stay clean.
    for (const int client_fd : viewer_client_fds) {
        if (client_fd == source_fd) {
            continue;
        }

        send_line(client_fd, line, counters);
    }
}

std::string make_metrics_line(
    ServerCounters& counters,
    const std::unordered_set<int>& active_client_fds,
    const DeviceStatusStore& status_store,
    std::uint64_t current_ms)
{
    const auto elapsed_ms = std::max<std::uint64_t>(1, current_ms - counters.last_report_ms);
    const auto messages_delta = counters.status_received - counters.last_report_status_received;
    const auto msg_per_sec = static_cast<std::uint64_t>(
        static_cast<double>(messages_delta) * 1000.0 / static_cast<double>(elapsed_ms));

    ServerMetrics metrics{};
    metrics.uptime_sec = (current_ms - counters.start_ms) / 1000;
    metrics.active = active_client_fds.size();
    metrics.accepted = counters.total_accepted;
    metrics.disconnected = counters.total_disconnected;
    metrics.devices = status_store.size();
    metrics.received = counters.status_received;
    metrics.ack_sent = counters.ack_sent;
    metrics.parse_errors = counters.parse_errors;
    metrics.broadcast_errors = counters.broadcast_errors;
    metrics.msg_per_sec = msg_per_sec;
    return format_metrics_line(metrics);
}

void print_metrics_if_due(
    ServerCounters& counters,
    const std::unordered_set<int>& active_client_fds,
    const std::unordered_set<int>& viewer_client_fds,
    const DeviceStatusStore& status_store)
{
    const auto current_ms = now_ms();
    if (current_ms - counters.last_report_ms < 1000) {
        return;
    }

    // Server metrics are intentionally server-side: connection count and
    // throughput live here, while client-observed p50/p99 stays in loadgen.
    const auto metrics_line =
        make_metrics_line(counters, active_client_fds, status_store, current_ms);
    std::cout << metrics_line;
    broadcast_to_viewers(viewer_client_fds, -1, metrics_line, counters);

    counters.last_report_ms = current_ms;
    counters.last_report_status_received = counters.status_received;
}

bool handle_hello_line(std::string_view line, int client_fd, std::unordered_set<int>& viewer_client_fds)
{
    if (!line.starts_with("HELLO ")) {
        return false;
    }

    if (line.find("role=viewer") != std::string_view::npos) {
        viewer_client_fds.insert(client_fd);
        std::cout << "Viewer registered: fd=" << client_fd << '\n';
    }

    return true;
}

void run_event_loop(int epoll_fd, int listen_fd, MqttBridge& mqtt_bridge)
{
    DeviceStatusStore status_store;
    std::unordered_map<int, std::string> client_buffers;
    std::unordered_set<int> telemetry_client_fds;
    std::unordered_set<int> viewer_client_fds;
    ServerCounters counters;
    counters.start_ms = now_ms();
    counters.last_report_ms = counters.start_ms;
    std::array<epoll_event, kMaxEvents> events{};

    while (!g_should_stop) {
        const int event_count =
            ::epoll_wait(epoll_fd, events.data(), events.size(), 1000);

        if (event_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::strerror(errno));
        }

        for (int i = 0; i < event_count; ++i) {
            const int fd = events[static_cast<std::size_t>(i)].data.fd;

            if (fd == listen_fd) {
                accept_ready_clients(
                    epoll_fd, listen_fd, client_buffers, telemetry_client_fds, counters);
                continue;
            }

            auto found_buffer = client_buffers.find(fd);
            if (found_buffer == client_buffers.end()) {
                remove_epoll_fd(epoll_fd, fd);
                continue;
            }

            auto& input_buffer = found_buffer->second;
            const bool still_connected = receive_available_data(fd, input_buffer);

            while (auto line = take_next_line(input_buffer)) {
                if (handle_hello_line(*line, fd, viewer_client_fds)) {
                    continue;
                }

                const auto status = handle_status_line(*line, status_store, counters);
                if (status.has_value()) {
                    if (status->ack_requested) {
                        if (send_ack(fd, *status)) {
                            ++counters.ack_sent;
                        }
                    } else {
                        broadcast_to_viewers(viewer_client_fds, fd, *line, counters);
                    }
                    mqtt_bridge.publish_status(*status);
                }
            }

            if (!still_connected) {
                std::cout << "Client disconnected: fd=" << fd << '\n';
                client_buffers.erase(fd);
                telemetry_client_fds.erase(fd);
                viewer_client_fds.erase(fd);
                ++counters.total_disconnected;
                remove_epoll_fd(epoll_fd, fd);
            }
        }

        print_metrics_if_due(counters, telemetry_client_fds, viewer_client_fds, status_store);
    }
}

} // namespace rdvc

int main()
{
    try {
        std::signal(SIGINT, rdvc::handle_signal);
        std::signal(SIGTERM, rdvc::handle_signal);

        auto telemetry_socket = rdvc::create_listening_socket();
        rdvc::Epoll epoll;
        rdvc::add_epoll_fd(epoll.fd(), telemetry_socket.fd());
        rdvc::MqttBridge mqtt_bridge;
        mqtt_bridge.start(rdvc::kMqttHost, rdvc::kMqttPort);

        std::cout << "RDVC server listening on " << rdvc::kListenAddress << ':'
                  << rdvc::kListenPort << '\n'
                  << std::flush;

        rdvc::run_event_loop(epoll.fd(), telemetry_socket.fd(), mqtt_bridge);
        std::cout << "RDVC server stopped\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "server error: " << error.what() << '\n';
        return 1;
    }
}
