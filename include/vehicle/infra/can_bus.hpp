#pragma once

#include "vehicle/core/vehicle_state.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vehicle::infra {

struct CanBusConfig {
    bool enabled{false};
    std::string interfaceName{"vcan0"};
    bool enableCanFd{false};
    int receiveTimeoutMs{100};
};

struct CanRawFrame {
    std::uint32_t id{0};
    bool extended{false};
    std::vector<std::uint8_t> data;
};

class CanBusClient {
public:
    explicit CanBusClient(CanBusConfig config);
    ~CanBusClient();

    CanBusClient(const CanBusClient&) = delete;
    CanBusClient& operator=(const CanBusClient&) = delete;

    [[nodiscard]] bool start();
    void stop();

    [[nodiscard]] core::VehicleState snapshot() const;
    [[nodiscard]] bool send(const CanRawFrame& frame) const;

    static void applyFrameToState(const CanRawFrame& frame, core::VehicleState& state);

private:
    void receiveLoop();
    void applyFrame(const CanRawFrame& frame);

    CanBusConfig config_;
    mutable std::mutex stateMutex_;
    core::VehicleState state_;
    std::atomic<bool> running_{false};
    std::thread receiveThread_;
    int socketFd_{-1};
};

} // namespace vehicle::infra
