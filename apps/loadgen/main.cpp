#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rdvc {
namespace {

constexpr std::string_view kDefaultHost = "127.0.0.1";
constexpr int kDefaultPort = 5000;
constexpr int kDefaultConnections = 100;
constexpr int kMaxEvents = 128;

struct Options {
    std::string host = std::string(kDefaultHost);
    int port = kDefaultPort;
    int connections = kDefaultConnections;
};

enum class ClientPhase {
    Connecting,
    WaitingAck,
};

struct ClientState {
    int fd = -1;
    int index = 0;
    std::uint64_t sequence = 1;
    std::uint64_t sent_ms = 0;
    std::string receive_buffer;
    ClientPhase phase = ClientPhase::Connecting;
};

std::uint64_t now_ms()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

Options parse_options(int argc, char* argv[])
{
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg = argv[index];
        auto require_value = [&](std::string_view name) -> std::string_view {
            if (index + 1 >= argc) {
                throw std::runtime_error("missing value for " + std::string(name));
            }
            return argv[++index];
        };

        if (arg == "--host") {
            options.host = std::string(require_value(arg));
        } else if (arg == "--port") {
            options.port = std::stoi(std::string(require_value(arg)));
        } else if (arg == "--connections") {
            options.connections = std::stoi(std::string(require_value(arg)));
        } else {
            throw std::runtime_error("unknown option: " + std::string(arg));
        }
    }

    if (options.connections <= 0) {
        throw std::runtime_error("--connections must be positive");
    }

    return options;
}

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

int create_epoll()
{
    const int fd = ::epoll_create1(EPOLL_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error(std::strerror(errno));
    }
    return fd;
}

void update_epoll(int epoll_fd, int fd, std::uint32_t events, int operation)
{
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;

    if (::epoll_ctl(epoll_fd, operation, fd, &event) < 0) {
        throw std::runtime_error(std::strerror(errno));
    }
}

void close_client(int epoll_fd, std::unordered_map<int, ClientState>& clients, int fd)
{
    ::epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    clients.erase(fd);
}

std::string device_id_for(int index)
{
    std::string id = "load-";
    id += std::to_string(index);
    return id;
}

std::string make_status_line(const ClientState& client)
{
    const auto device_id = device_id_for(client.index);
    const int x = client.index % 50;
    const int y = (client.index / 50) % 50;
    const int z = client.index % 10;

    std::string line = "STATUS device_id=";
    line += device_id;
    line += " state=OK battery=87 x=";
    line += std::to_string(x);
    line += " y=";
    line += std::to_string(y);
    line += " z=";
    line += std::to_string(z);
    line += " seq=";
    line += std::to_string(client.sequence);
    line += " sent_ms=";
    line += std::to_string(client.sent_ms);
    line += " ack=1\n";
    return line;
}

bool has_ack_line(std::string& receive_buffer)
{
    const auto newline = receive_buffer.find('\n');
    if (newline == std::string::npos) {
        return false;
    }

    const auto line = receive_buffer.substr(0, newline + 1);
    receive_buffer.erase(0, newline + 1);
    return line.starts_with("ACK ");
}

int start_connect(const Options& options)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error(std::strerror(errno));
    }

    set_non_blocking(fd);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);
    if (::inet_pton(AF_INET, options.host.c_str(), &address.sin_addr) != 1) {
        ::close(fd);
        throw std::runtime_error("invalid --host address");
    }

    const int result = ::connect(
        fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (result < 0 && errno != EINPROGRESS) {
        const std::string error = std::strerror(errno);
        ::close(fd);
        throw std::runtime_error(error);
    }

    return fd;
}

bool finish_connect(int fd)
{
    int socket_error = 0;
    socklen_t socket_error_size = sizeof(socket_error);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &socket_error_size) < 0) {
        throw std::runtime_error(std::strerror(errno));
    }

    return socket_error == 0;
}

bool send_status(ClientState& client)
{
    client.sent_ms = now_ms();
    const auto status_line = make_status_line(client);
    const ssize_t sent = ::send(client.fd, status_line.data(), status_line.size(), MSG_NOSIGNAL);

    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false;
        }
        throw std::runtime_error(std::strerror(errno));
    }

    if (static_cast<std::size_t>(sent) != status_line.size()) {
        throw std::runtime_error("partial send is not handled in loadgen v1");
    }

    client.phase = ClientPhase::WaitingAck;
    return true;
}

std::optional<std::uint64_t> receive_ack(ClientState& client)
{
    std::array<char, 512> buffer{};

    while (true) {
        const ssize_t received = ::recv(client.fd, buffer.data(), buffer.size(), 0);
        if (received > 0) {
            client.receive_buffer.append(buffer.data(), static_cast<std::size_t>(received));
            if (has_ack_line(client.receive_buffer)) {
                return now_ms() - client.sent_ms;
            }
            continue;
        }

        if (received == 0) {
            throw std::runtime_error("server closed before ACK");
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return std::nullopt;
        }

        throw std::runtime_error(std::strerror(errno));
    }
}

std::uint64_t percentile(std::vector<std::uint64_t> values, double ratio)
{
    if (values.empty()) {
        return 0;
    }

    std::sort(values.begin(), values.end());
    const auto raw_index = static_cast<std::size_t>((values.size() - 1) * ratio);
    return values[raw_index];
}

void print_summary(
    const Options& options,
    std::uint64_t started_ms,
    const std::vector<std::uint64_t>& latencies,
    int errors)
{
    const auto elapsed_ms = std::max<std::uint64_t>(1, now_ms() - started_ms);
    const double throughput = static_cast<double>(latencies.size()) * 1000.0
        / static_cast<double>(elapsed_ms);

    std::cout << "connections: " << options.connections << '\n';
    std::cout << "acked: " << latencies.size() << '\n';
    std::cout << "errors: " << errors << '\n';
    std::cout << "elapsed_ms: " << elapsed_ms << '\n';
    std::cout << "throughput_ack_per_sec: " << std::fixed << std::setprecision(1)
              << throughput << '\n';
    std::cout << "latency_ms_p50: " << percentile(latencies, 0.50) << '\n';
    std::cout << "latency_ms_p95: " << percentile(latencies, 0.95) << '\n';
    std::cout << "latency_ms_p99: " << percentile(latencies, 0.99) << '\n';
}

int run_loadgen(const Options& options)
{
    const int epoll_fd = create_epoll();
    std::unordered_map<int, ClientState> clients;
    std::vector<std::uint64_t> latencies;
    latencies.reserve(static_cast<std::size_t>(options.connections));
    int errors = 0;

    const auto started_ms = now_ms();

    for (int index = 0; index < options.connections; ++index) {
        const int fd = start_connect(options);
        ClientState client{};
        client.fd = fd;
        client.index = index;
        clients.emplace(fd, std::move(client));
        update_epoll(epoll_fd, fd, EPOLLOUT | EPOLLERR | EPOLLHUP, EPOLL_CTL_ADD);
    }

    std::array<epoll_event, kMaxEvents> events{};
    while (!clients.empty()) {
        const int event_count = ::epoll_wait(epoll_fd, events.data(), events.size(), 5000);
        if (event_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::strerror(errno));
        }

        if (event_count == 0) {
            errors += static_cast<int>(clients.size());
            for (auto iterator = clients.begin(); iterator != clients.end();) {
                const int fd = iterator->first;
                ++iterator;
                close_client(epoll_fd, clients, fd);
            }
            break;
        }

        for (int event_index = 0; event_index < event_count; ++event_index) {
            const int fd = events[static_cast<std::size_t>(event_index)].data.fd;
            auto found = clients.find(fd);
            if (found == clients.end()) {
                continue;
            }

            try {
                auto& client = found->second;
                const auto flags = events[static_cast<std::size_t>(event_index)].events;
                if ((flags & (EPOLLERR | EPOLLHUP)) != 0) {
                    throw std::runtime_error("socket error event");
                }

                if (client.phase == ClientPhase::Connecting && (flags & EPOLLOUT) != 0) {
                    if (!finish_connect(fd)) {
                        throw std::runtime_error("connect failed");
                    }

                    if (send_status(client)) {
                        update_epoll(epoll_fd, fd, EPOLLIN | EPOLLERR | EPOLLHUP, EPOLL_CTL_MOD);
                    }
                    continue;
                }

                if (client.phase == ClientPhase::WaitingAck && (flags & EPOLLIN) != 0) {
                    if (auto latency = receive_ack(client)) {
                        latencies.push_back(*latency);
                        close_client(epoll_fd, clients, fd);
                    }
                }
            } catch (const std::exception& error) {
                ++errors;
                close_client(epoll_fd, clients, fd);
            }
        }
    }

    ::close(epoll_fd);
    print_summary(options, started_ms, latencies, errors);
    return errors == 0 ? 0 : 1;
}

} // namespace
} // namespace rdvc

int main(int argc, char* argv[])
{
    try {
        const auto options = rdvc::parse_options(argc, argv);
        return rdvc::run_loadgen(options);
    } catch (const std::exception& error) {
        std::cerr << "loadgen error: " << error.what() << '\n';
        std::cerr << "usage: rdvc_loadgen [--host 127.0.0.1] [--port 5000] "
                  << "[--connections 100]\n";
        return 1;
    }
}
