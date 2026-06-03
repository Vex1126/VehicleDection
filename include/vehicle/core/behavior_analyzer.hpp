#pragma once

#include "vehicle/core/types.hpp"
#include "vehicle/core/vehicle_state.hpp"

#include <vector>

namespace vehicle::core {

class BehaviorAnalyzer {
public:
    [[nodiscard]] std::vector<BehaviorEvent> analyze(const std::vector<Track>& tracks,
                                                     const Frame& frame) const;
    [[nodiscard]] std::vector<BehaviorEvent> analyze(const std::vector<Track>& tracks,
                                                     const Frame& frame,
                                                     const VehicleState& vehicleState) const;

private:
    [[nodiscard]] bool isVehicle(ObjectClass label) const;
};

} // namespace vehicle::core
