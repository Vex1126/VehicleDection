#include "vehicle/infra/can_bus.hpp"

#include "vehicle/infra/logger.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

#if defined(__linux__)
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace vehicle::infra {

namespace {

std::uint16_t readU16Be(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[offset]) << 8U) |
                                      static_cast<std::uint16_t>(data[offset + 1]));
}

std::int16_t readI16Be(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    return static_cast<std::int16_t>(readU16Be(data, offset));
}

core::TurnSignal parseTurnSignal(std::uint8_t value)
{
    switch (value) {
    case 0:
        return core::TurnSignal::None;
    case 1:
        return core::TurnSignal::Left;
    case 2:
        return core::TurnSignal::Right;
    case 3:
        return core::TurnSignal::Hazard;
    default:
        return core::TurnSignal::Unknown;
    }
}

#if defined(__linux__)
CanRawFrame fromSocketCanFrame(const can_frame& frame)
{
    CanRawFrame raw;
    raw.id = frame.can_id & CAN_EFF_MASK;
    raw.extended = (frame.can_id & CAN_EFF_FLAG) != 0;
    raw.data.assign(frame.data, frame.data + frame.can_dlc);
    return raw;
}

can_frame toSocketCanFrame(const CanRawFrame& raw)
{
    can_frame frame{};
    frame.can_id = raw.id | (raw.extended ? CAN_EFF_FLAG : 0U);
    frame.can_dlc = static_cast<__u8>(std::min<std::size_t>(raw.data.size(), CAN_MAX_DLEN));
    if (!raw.data.empty()) {
        std::memcpy(frame.data, raw.data.data(), frame.can_dlc);
    }
    return frame;
}
#endif

} // namespace

CanBusClient::CanBusClient(CanBusConfig config) : config_(std::move(config)) {}

CanBusClient::~CanBusClient()
{
    stop();
}

bool CanBusClient::start()
{
    if (!config_.enabled) {
        return true;
    }
    if (running_) {
        return true;
    }

#if defined(__linux__)
    socketFd_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socketFd_ < 0) {
        Logger::instance().log(LogLevel::Warning, "CAN socket open failed: " + std::string(std::strerror(errno)));
        return false;
    }

    ifreq ifr{};
    std::snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", config_.interfaceName.c_str());
    if (::ioctl(socketFd_, SIOCGIFINDEX, &ifr) < 0) {
        Logger::instance().log(LogLevel::Warning,
                               "CAN interface lookup failed for " + config_.interfaceName + ": " +
                                   std::string(std::strerror(errno)));
        ::close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(socketFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        Logger::instance().log(LogLevel::Warning,
                               "CAN bind failed for " + config_.interfaceName + ": " +
                                   std::string(std::strerror(errno)));
        ::close(socketFd_);
        socketFd_ = -1;
        return false;
    }

    timeval timeout{};
    timeout.tv_sec = config_.receiveTimeoutMs / 1000;
    timeout.tv_usec = (config_.receiveTimeoutMs % 1000) * 1000;
    ::setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    running_ = true;
    receiveThread_ = std::thread(&CanBusClient::receiveLoop, this);
    Logger::instance().log(LogLevel::Info, "CAN bus started on " + config_.interfaceName);
    return true;
#else
    Logger::instance().log(LogLevel::Warning, "CAN bus is only available on Linux SocketCAN");
    return false;
#endif
}

void CanBusClient::stop()
{
    running_ = false;
#if defined(__linux__)
    if (socketFd_ >= 0) {
        ::shutdown(socketFd_, SHUT_RDWR);
    }
#endif
    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
#if defined(__linux__)
    if (socketFd_ >= 0) {
        ::close(socketFd_);
        socketFd_ = -1;
    }
#endif
}

core::VehicleState CanBusClient::snapshot() const
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    return state_;
}

bool CanBusClient::send(const CanRawFrame& frame) const
{
    if (!config_.enabled) {
        return false;
    }
#if defined(__linux__)
    if (socketFd_ < 0) {
        return false;
    }
    const auto socketFrame = toSocketCanFrame(frame);
    return ::write(socketFd_, &socketFrame, sizeof(socketFrame)) == static_cast<ssize_t>(sizeof(socketFrame));
#else
    (void)frame;
    return false;
#endif
}

void CanBusClient::applyFrameToState(const CanRawFrame& frame, core::VehicleState& state)
{
    switch (frame.id) {
    case 0x100:
        if (frame.data.size() >= 2) {
            state.speedKph = static_cast<double>(readU16Be(frame.data, 0)) * 0.01;
        }
        break;
    case 0x101:
        if (frame.data.size() >= 2) {
            state.steeringAngleDeg = static_cast<double>(readI16Be(frame.data, 0)) * 0.1;
        }
        break;
    case 0x102:
        if (frame.data.size() >= 2) {
            state.brakePressed = frame.data[0] != 0;
            state.throttlePercent = static_cast<double>(frame.data[1]);
        }
        break;
    case 0x103:
        if (frame.data.size() >= 2) {
            state.gear = static_cast<int>(static_cast<std::int8_t>(frame.data[0]));
            state.turnSignal = parseTurnSignal(frame.data[1]);
        }
        break;
    default:
        return;
    }

    state.valid = true;
    state.timestamp = std::chrono::steady_clock::now();
}

void CanBusClient::receiveLoop()
{
#if defined(__linux__)
    while (running_) {
        can_frame frame{};
        const auto bytes = ::read(socketFd_, &frame, sizeof(frame));
        if (bytes == static_cast<ssize_t>(sizeof(frame))) {
            applyFrame(fromSocketCanFrame(frame));
        }
    }
#endif
}

void CanBusClient::applyFrame(const CanRawFrame& frame)
{
    std::lock_guard<std::mutex> lock(stateMutex_);
    applyFrameToState(frame, state_);
}

} // namespace vehicle::infra
