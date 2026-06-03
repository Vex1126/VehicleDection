#include "vehicle/core/behavior_analyzer.hpp"

#include <cmath>
#include <sstream>
#include <utility>

namespace vehicle::core {

namespace {

BehaviorEvent makeEvent(const Track& track, std::string behavior, RiskLevel risk, std::string evidence)
{
    const std::string objectClass =
        track.detection.className.empty() ? toString(track.detection.label) : track.detection.className;
    return {track.id, std::move(behavior), risk, std::move(evidence), track.detection.box, objectClass};
}

} // namespace

bool BehaviorAnalyzer::isVehicle(ObjectClass label) const
{
    return label == ObjectClass::Car || label == ObjectClass::Truck || label == ObjectClass::Bus ||
           label == ObjectClass::Motorcycle;
}

std::vector<BehaviorEvent> BehaviorAnalyzer::analyze(const std::vector<Track>& tracks,
                                                     const Frame& frame) const
{
    return analyze(tracks, frame, {});
}

std::vector<BehaviorEvent> BehaviorAnalyzer::analyze(const std::vector<Track>& tracks,
                                                     const Frame& frame,
                                                     const VehicleState& vehicleState) const
{
    std::vector<BehaviorEvent> events;
    const double laneCenter = static_cast<double>(frame.width) / 2.0;
    const double laneHalfWidth = static_cast<double>(frame.width) * 0.16;

    for (const auto& track : tracks) {
        const auto center = track.detection.box.center();
        const double boxAreaRatio =
            track.detection.box.area() / static_cast<double>(frame.width * frame.height);

        if (isVehicle(track.detection.label)) {
            if (std::abs(center.x - laneCenter) < laneHalfWidth && boxAreaRatio > 0.018 &&
                track.velocity.y > 5.0) {
                std::ostringstream evidence;
                evidence << "front object grows in ego lane, vy=" << track.velocity.y
                         << ", area_ratio=" << boxAreaRatio;
                RiskLevel risk = RiskLevel::Critical;
                if (vehicleState.valid && vehicleState.speedKph.has_value()) {
                    evidence << ", ego_speed_kph=" << *vehicleState.speedKph;
                    risk = *vehicleState.speedKph >= 35.0 ? RiskLevel::Critical : RiskLevel::High;
                }
                if (vehicleState.valid && vehicleState.brakePressed == true) {
                    evidence << ", ego_brake_pressed=true";
                }
                events.push_back(makeEvent(track, "front_collision_risk", risk, evidence.str()));
            }

            if (std::abs(track.velocity.x) > 10.0 && center.y > frame.height * 0.30 &&
                center.y < frame.height * 0.78) {
                std::ostringstream evidence;
                evidence << "large lateral velocity inside drivable region";
                RiskLevel risk = RiskLevel::High;
                if (vehicleState.valid && (vehicleState.turnSignal == TurnSignal::Left ||
                                           vehicleState.turnSignal == TurnSignal::Right)) {
                    evidence << ", ego_turn_signal=" << toString(vehicleState.turnSignal);
                    risk = RiskLevel::Medium;
                }
                events.push_back(makeEvent(track,
                                           "cut_in_or_abrupt_lane_change",
                                           risk,
                                           evidence.str()));
            }

            if (track.trajectory.size() >= 4) {
                const double dy1 = track.trajectory[track.trajectory.size() - 2].y -
                                   track.trajectory[track.trajectory.size() - 4].y;
                const double dy2 = track.trajectory.back().y -
                                   track.trajectory[track.trajectory.size() - 2].y;
                if (dy1 > 4.0 && dy2 < 1.0) {
                    events.push_back(makeEvent(track,
                                               "sudden_stop_ahead",
                                               RiskLevel::High,
                                               "forward movement drops sharply"));
                }
            }

            if (center.x < frame.width * 0.12 || center.x > frame.width * 0.88) {
                events.push_back(makeEvent(track,
                                           "lane_departure_or_road_edge_risk",
                                           RiskLevel::Medium,
                                           "tracked vehicle is close to road edge"));
            }
        }

        if (track.detection.label == ObjectClass::Pedestrian &&
            center.x > frame.width * 0.28 && center.x < frame.width * 0.72 &&
            center.y > frame.height * 0.38) {
            events.push_back(makeEvent(track,
                                       "pedestrian_in_path",
                                       RiskLevel::High,
                                       "pedestrian appears in the projected ego path"));
        }
    }

    return events;
}

} // namespace vehicle::core
