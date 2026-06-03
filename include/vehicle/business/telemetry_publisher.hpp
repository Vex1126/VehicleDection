#pragma once

#include "vehicle/core/types.hpp"
#include "vehicle/core/vehicle_state.hpp"
#include "vehicle/infra/blocking_queue.hpp"
#include "vehicle/infra/mqtt_client.hpp"
#include "vehicle/infra/performance_monitor.hpp"

#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace vehicle::business {

struct TelemetryConfig {
    bool enabled{false};
    std::string topicPrefix{"vehicle/vehicle-001"};
    std::size_t queueCapacity{128};
    infra::MqttConfig mqtt;
};

class TelemetryPublisher {
public:
    explicit TelemetryPublisher(TelemetryConfig config);
    ~TelemetryPublisher();

    TelemetryPublisher(const TelemetryPublisher&) = delete;
    TelemetryPublisher& operator=(const TelemetryPublisher&) = delete;

    [[nodiscard]] bool start();
    void stop();

    void publishFrameSummary(int frameIndex,
                             std::size_t detectionCount,
                             std::size_t trackCount,
                             double fps,
                             const std::vector<core::BehaviorEvent>& events,
                             const core::VehicleState& vehicleState);
    void publishStatus(const infra::PerformanceSnapshot& performance, int processedFrames);

private:
    struct Message {
        std::string topic;
        std::string payload;
    };

    void publishAsync(std::string topic, std::string payload);
    void run();

    TelemetryConfig config_;
    infra::BlockingQueue<Message> queue_;
    infra::MqttClient mqtt_;
    std::thread thread_;
    bool started_{false};
};

[[nodiscard]] std::string behaviorEventsToJson(const std::vector<core::BehaviorEvent>& events);

} // namespace vehicle::business
