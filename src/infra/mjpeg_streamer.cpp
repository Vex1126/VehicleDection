#include "vehicle/infra/mjpeg_streamer.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace vehicle::infra {

namespace {

constexpr char kStreamHeader[] =
    "HTTP/1.1 200 OK\r\n"
    "Connection: close\r\n"
    "Cache-Control: no-cache, no-store, must-revalidate\r\n"
    "Pragma: no-cache\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=vehicleframe\r\n\r\n";

void closeSocket(int fd)
{
    if (fd >= 0) {
        ::close(fd);
    }
}

bool writePayload(int fd, const unsigned char* bytes, std::size_t size)
{
    std::size_t sent = 0;
    while (sent < size) {
        const auto result = ::send(fd, bytes + sent, size - sent, MSG_NOSIGNAL);
        if (result > 0) {
            sent += static_cast<std::size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return false;
        }
        return false;
    }
    return true;
}

bool writeText(int fd, const std::string& text)
{
    return writePayload(fd, reinterpret_cast<const unsigned char*>(text.data()), text.size());
}

void setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

void setSendTimeout(int fd)
{
    timeval timeout{};
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    (void)::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

} // namespace

MjpegStreamer::~MjpegStreamer()
{
    stop();
}

bool MjpegStreamer::start(int port, std::string* errorMessage)
{
    if (listenFd_ >= 0) {
        return true;
    }
    if (port <= 0 || port > 65535) {
        if (errorMessage != nullptr) {
            *errorMessage = "invalid TCP port";
        }
        return false;
    }

    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = std::strerror(errno);
        }
        return false;
    }

    int reuseAddress = 1;
    (void)::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::bind(listenFd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listenFd_, 4) != 0) {
        if (errorMessage != nullptr) {
            *errorMessage = std::strerror(errno);
        }
        closeSocket(listenFd_);
        listenFd_ = -1;
        return false;
    }

    setNonBlocking(listenFd_);
    stopping_ = false;
    thread_ = std::thread(&MjpegStreamer::serve, this);
    return true;
}

void MjpegStreamer::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    frameReady_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    closeSocket(listenFd_);
    listenFd_ = -1;
}

void MjpegStreamer::publish(std::vector<unsigned char> jpegFrame)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        latestFrame_ = std::move(jpegFrame);
        ++latestSequence_;
    }
    frameReady_.notify_one();
}

void MjpegStreamer::serve()
{
    std::vector<int> clients;
    std::uint64_t sentSequence = 0;

    while (true) {
        while (true) {
            const int client = ::accept(listenFd_, nullptr, nullptr);
            if (client < 0) {
                break;
            }
            setSendTimeout(client);
            setNonBlocking(client);
            if (writeText(client, kStreamHeader)) {
                clients.push_back(client);
            } else {
                closeSocket(client);
            }
        }

        std::vector<unsigned char> frame;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            frameReady_.wait_for(lock, std::chrono::milliseconds(100), [this, sentSequence] {
                return stopping_ || latestSequence_ != sentSequence;
            });
            if (stopping_) {
                break;
            }
            if (latestSequence_ == sentSequence || latestFrame_.empty()) {
                continue;
            }
            frame = latestFrame_;
            sentSequence = latestSequence_;
        }

        std::ostringstream boundary;
        boundary << "--vehicleframe\r\n"
                 << "Content-Type: image/jpeg\r\n"
                 << "Content-Length: " << frame.size() << "\r\n\r\n";
        const std::string prefix = boundary.str();
        const std::string suffix = "\r\n";

        auto client = clients.begin();
        while (client != clients.end()) {
            if (!writeText(*client, prefix) || !writePayload(*client, frame.data(), frame.size()) ||
                !writeText(*client, suffix)) {
                closeSocket(*client);
                client = clients.erase(client);
            } else {
                ++client;
            }
        }
    }

    for (const int client : clients) {
        closeSocket(client);
    }
}

} // namespace vehicle::infra
