#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace vehicle::core {

enum class TurnSignal {
    None,
    Left,
    Right,
    Hazard,
    Unknown
};

struct VehicleState {
    bool valid{false};
    std::optional<double> speedKph;
    std::optional<double> steeringAngleDeg;
    std::optional<bool> brakePressed;
    std::optional<double> throttlePercent;
    std::optional<int> gear;
    TurnSignal turnSignal{TurnSignal::Unknown};
    std::chrono::steady_clock::time_point timestamp{std::chrono::steady_clock::now()};
};

[[nodiscard]] const char* toString(TurnSignal signal);
[[nodiscard]] std::string toJson(const VehicleState& state);

} // namespace vehicle::core
