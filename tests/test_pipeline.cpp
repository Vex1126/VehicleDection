#include "vehicle/business/analysis_pipeline.hpp"
#include "vehicle/business/telemetry_publisher.hpp"
#include "vehicle/core/tracker.hpp"
#include "vehicle/infra/blocking_queue.hpp"
#include "vehicle/infra/can_bus.hpp"
#include "vehicle/infra/frame_cache.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <thread>

namespace {

void testTrackerKeepsStableId()
{
    vehicle::core::SortTracker tracker;
    const auto first = tracker.update({{{100.0, 100.0, 80.0, 60.0}, 0.9F, vehicle::core::ObjectClass::Car}});
    const auto second = tracker.update({{{108.0, 105.0, 80.0, 60.0}, 0.9F, vehicle::core::ObjectClass::Car}});

    assert(first.size() == 1);
    assert(second.size() == 1);
    assert(first.front().id == second.front().id);
}


void testCanFrameDecoding()
{
    vehicle::core::VehicleState state;
    vehicle::infra::CanBusClient::applyFrameToState({0x100, false, {0x13, 0x88}}, state);
    vehicle::infra::CanBusClient::applyFrameToState({0x101, false, {0xFF, 0x9C}}, state);
    vehicle::infra::CanBusClient::applyFrameToState({0x102, false, {0x01, 0x32}}, state);
    vehicle::infra::CanBusClient::applyFrameToState({0x103, false, {0x03, 0x01}}, state);

    assert(state.valid);
    assert(state.speedKph.has_value());
    assert(*state.speedKph == 50.0);
    assert(state.steeringAngleDeg.has_value());
    assert(*state.steeringAngleDeg == -10.0);
    assert(state.brakePressed.has_value());
    assert(*state.brakePressed);
    assert(state.throttlePercent.has_value());
    assert(*state.throttlePercent == 50.0);
    assert(state.gear.has_value());
    assert(*state.gear == 3);
    assert(state.turnSignal == vehicle::core::TurnSignal::Left);
}

void testTelemetryEventJson()
{
    const vehicle::core::BehaviorEvent event{7,
                                             "front_collision_risk",
                                             vehicle::core::RiskLevel::Critical,
                                             "ego_speed_kph=50",
                                             {1.0, 2.0, 3.0, 4.0},
                                             "car"};
    const auto json = vehicle::business::behaviorEventsToJson({event});
    assert(json.find("front_collision_risk") != std::string::npos);
    assert(json.find("critical") != std::string::npos);
    assert(json.find("ego_speed_kph=50") != std::string::npos);
}

void testPipelineFindsRisks()
{
    vehicle::business::PipelineConfig config;
    config.frameCount = 36;
    config.device = "cpu";
    vehicle::business::AnalysisPipeline pipeline(config);

    const auto result = pipeline.run();
    assert(result.processedFrames == 36);
    assert(!result.events.empty());

    bool foundCollision = false;
    bool foundPedestrian = false;
    for (const auto& event : result.events) {
        foundCollision = foundCollision || event.behavior == "front_collision_risk";
        foundPedestrian = foundPedestrian || event.behavior == "pedestrian_in_path";
    }
    assert(foundCollision);
    assert(foundPedestrian);
}

void testFrameCacheEvictsOldFrames()
{
    vehicle::infra::FrameCache<int> cache(2);
    cache.push(1);
    cache.push(2);
    cache.push(3);

    assert(cache.size() == 2);
    const auto latest = cache.latest();
    assert(latest.has_value());
    assert(*latest == 3);
    const auto snapshot = cache.snapshot();
    assert(snapshot.front() == 2);
    assert(snapshot.back() == 3);
}

void testBlockingQueueTransfersBetweenThreads()
{
    vehicle::infra::BlockingQueue<int> queue(1);
    int received = 0;
    std::thread consumer([&] {
        const auto value = queue.pop();
        assert(value.has_value());
        received = *value;
    });

    assert(queue.push(42));
    queue.close();
    consumer.join();
    assert(received == 42);
}

} // namespace

int main()
{
    testTrackerKeepsStableId();
    testCanFrameDecoding();
    testTelemetryEventJson();
    testFrameCacheEvictsOldFrames();
    testBlockingQueueTransfersBetweenThreads();
    testPipelineFindsRisks();
    std::cout << "vehicle_tests passed\n";
    return 0;
}
