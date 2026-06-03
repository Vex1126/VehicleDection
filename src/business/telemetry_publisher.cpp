#include "vehicle/business/telemetry_publisher.hpp"

#include "vehicle/infra/logger.hpp"

#include <sstream>
#include <utility>

namespace vehicle::business {

namespace {

std::string escapeJsonString(const std::string& value)
{
    std::ostringstream escaped;
    for (const char ch : value) {
        switch (ch) {
        case '"':
            escaped << "\\\"";
            break;
        case '\\':
            escaped << "\\\\";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                escaped << "\\u00";
                constexpr char digits[] = "0123456789abcdef";
                escaped << digits[(static_cast<unsigned char>(ch) >> 4U) & 0x0FU]
                        << digits[static_cast<unsigned char>(ch) & 0x0FU];
            } else {
                escaped << ch;
            }
        }
    }
    return escaped.str();
}

std::string eventToJson(const core::BehaviorEvent& event)
{
    std::ostringstream json;
    json << "{\"track_id\":" << event.trackId << ",\"behavior\":\"" << escapeJsonString(event.behavior)
         << "\",\"risk\":\"" << core::toString(event.risk) << "\",\"evidence\":\""
         << escapeJsonString(event.evidence) << "\",\"object_class\":\"" << escapeJsonString(event.objectClass)
         << "\",\"bbox\":{\"x\":" << event.targetBox.x << ",\"y\":" << event.targetBox.y
         << ",\"width\":" << event.targetBox.width << ",\"height\":" << event.targetBox.height << "}}";
    return json.str();
}

std::string normalizeTopicPrefix(std::string prefix)
{
    while (!prefix.empty() && prefix.back() == '/') {
        prefix.pop_back();
    }
    return prefix.empty() ? "vehicle/vehicle-001" : prefix;
}

} // namespace

TelemetryPublisher::TelemetryPublisher(TelemetryConfig config)
    : config_(std::move(config)),
      queue_(config_.queueCapacity),
      mqtt_(config_.mqtt)
{
    config_.topicPrefix = normalizeTopicPrefix(config_.topicPrefix);
}

TelemetryPublisher::~TelemetryPublisher()
{
    stop();
}

bool TelemetryPublisher::start()
{
    if (!config_.enabled) {
        return true;
    }
    if (started_) {
        return true;
    }
    if (!mqtt_.connect()) {
        infra::Logger::instance().log(infra::LogLevel::Warning, "telemetry MQTT publisher did not connect");
    }
    started_ = true;
    thread_ = std::thread(&TelemetryPublisher::run, this);
    return true;
}

void TelemetryPublisher::stop()
{
    if (!started_) {
        return;
    }
    queue_.close();
    if (thread_.joinable()) {
        thread_.join();
    }
    mqtt_.disconnect();
    started_ = false;
}

void TelemetryPublisher::publishFrameSummary(int frameIndex,
                                             std::size_t detectionCount,
                                             std::size_t trackCount,
                                             double fps,
                                             const std::vector<core::BehaviorEvent>& events,
                                             const core::VehicleState& vehicleState)
{
    if (!config_.enabled) {
        return;
    }

    std::ostringstream summary;
    summary << "{\"frame_index\":" << frameIndex << ",\"detections\":" << detectionCount
            << ",\"tracks\":" << trackCount << ",\"fps\":" << fps
            << ",\"vehicle_state\":" << core::toJson(vehicleState)
            << ",\"events\":" << behaviorEventsToJson(events) << "}";
    publishAsync(config_.topicPrefix + "/frames", summary.str());

    for (const auto& event : events) {
        publishAsync(config_.topicPrefix + "/events", eventToJson(event));
    }

    if (vehicleState.valid) {
        publishAsync(config_.topicPrefix + "/can/state", core::toJson(vehicleState));
    }
}

void TelemetryPublisher::publishStatus(const infra::PerformanceSnapshot& performance, int processedFrames)
{
    if (!config_.enabled) {
        return;
    }

    std::ostringstream status;
    status << "{\"processed_frames\":" << processedFrames << ",\"fps\":" << performance.fps
           << ",\"avg_frame_ms\":" << performance.averageFrameMs << ",\"cpu_percent\":"
           << performance.cpuPercent << ",\"rss_bytes\":" << performance.memoryBytes << "}";
    publishAsync(config_.topicPrefix + "/status", status.str());
}

void TelemetryPublisher::publishAsync(std::string topic, std::string payload)
{
    if (!queue_.push({std::move(topic), std::move(payload)})) {
        infra::Logger::instance().log(infra::LogLevel::Warning, "telemetry queue is closed");
    }
}

void TelemetryPublisher::run()
{
    while (auto message = queue_.pop()) {
        if (!mqtt_.publish(message->topic, message->payload)) {
            infra::Logger::instance().log(infra::LogLevel::Warning,
                                          "failed to publish MQTT telemetry topic=" + message->topic);
        }
    }
}

std::string behaviorEventsToJson(const std::vector<core::BehaviorEvent>& events)
{
    std::ostringstream json;
    json << '[';
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (i != 0) {
            json << ',';
        }
        json << eventToJson(events[i]);
    }
    json << ']';
    return json.str();
}

} // namespace vehicle::business
