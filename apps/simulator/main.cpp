#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rdvc {

constexpr std::string_view kServerAddress = "127.0.0.1";
constexpr int kServerPort = 5000;

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

Socket connect_to_server()
{
    Socket socket{::socket(AF_INET, SOCK_STREAM, 0)};

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kServerPort);
    if (::inet_pton(AF_INET, kServerAddress.data(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid server address");
    }

    if (::connect(
            socket.fd(), reinterpret_cast<const sockaddr*>(&address), sizeof(address))
        < 0) {
        throw std::runtime_error(std::strerror(errno));
    }

    return socket;
}

std::string make_status_line(
    std::string_view device_id,
    std::string_view x,
    std::string_view y,
    std::string_view z)
{
    std::string line = "STATUS device_id=";
    line += device_id;
    line += " state=OK battery=87 x=";
    line += x;
    line += " y=";
    line += y;
    line += " z=";
    line += z;
    line += '\n';
    return line;
}

void send_status_line(int socket_fd, std::string_view status_line)
{
    const ssize_t sent =
        ::send(socket_fd, status_line.data(), status_line.size(), 0);

    if (sent < 0) {
        throw std::runtime_error(std::strerror(errno));
    }

    if (static_cast<std::size_t>(sent) != status_line.size()) {
        throw std::runtime_error("partial send is not handled in Phase 4");
    }
}

} // namespace rdvc

int main(int argc, char* argv[])
{
    try {
        const std::string_view device_id = argc >= 2 ? argv[1] : "sim-001";
        const std::string_view x = argc >= 3 ? argv[2] : "0";
        const std::string_view y = argc >= 4 ? argv[3] : "0";
        const std::string_view z = argc >= 5 ? argv[4] : "0";
        const auto status_line = rdvc::make_status_line(device_id, x, y, z);

        auto socket = rdvc::connect_to_server();
        rdvc::send_status_line(socket.fd(), status_line);

        std::cout << "Sent: " << status_line;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "simulator error: " << error.what() << '\n';
        return 1;
    }
}
