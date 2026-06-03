#include "vehicle/core/vehicle_state.hpp"

#include <sstream>

namespace vehicle::core {

const char* toString(TurnSignal signal)
{
    switch (signal) {
    case TurnSignal::None:
        return "none";
    case TurnSignal::Left:
        return "left";
    case TurnSignal::Right:
        return "right";
    case TurnSignal::Hazard:
        return "hazard";
    case TurnSignal::Unknown:
    default:
        return "unknown";
    }
}

std::string toJson(const VehicleState& state)
{
    std::ostringstream json;
    json << "{\"valid\":" << (state.valid ? "true" : "false");
    if (state.speedKph.has_value()) {
        json << ",\"speed_kph\":" << *state.speedKph;
    }
    if (state.steeringAngleDeg.has_value()) {
        json << ",\"steering_angle_deg\":" << *state.steeringAngleDeg;
    }
    if (state.brakePressed.has_value()) {
        json << ",\"brake_pressed\":" << (*state.brakePressed ? "true" : "false");
    }
    if (state.throttlePercent.has_value()) {
        json << ",\"throttle_percent\":" << *state.throttlePercent;
    }
    if (state.gear.has_value()) {
        json << ",\"gear\":" << *state.gear;
    }
    json << ",\"turn_signal\":\"" << toString(state.turnSignal) << "\"}";
    return json.str();
}

} // namespace vehicle::core
