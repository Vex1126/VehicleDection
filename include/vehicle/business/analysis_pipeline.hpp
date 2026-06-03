#pragma once

#include "vehicle/core/behavior_analyzer.hpp"
#include "vehicle/core/detector.hpp"
#include "vehicle/core/tracker.hpp"
#include "vehicle/core/visualizer.hpp"
#include "vehicle/core/video_source.hpp"
#include "vehicle/business/telemetry_publisher.hpp"
#include "vehicle/infra/blocking_queue.hpp"
#include "vehicle/infra/frame_cache.hpp"
#include "vehicle/infra/can_bus.hpp"
#include "vehicle/infra/http_client.hpp"
#include "vehicle/infra/performance_monitor.hpp"

#include <memory>
#include <string>
#include <vector>

namespace vehicle::business {

inline constexpr long kDefaultLlmHttpTimeoutMs = 65000;

struct PipelineConfig {
    std::string modelPath{"models/yolo11n.onnx"};
    std::string videoPath;
    std::string device{"cpu"};
    int frameCount{60};
    std::size_t frameQueueSize{8};
    std::size_t cacheCapacity{30};
    bool enableLlmAnalysis{false};
    bool displayEnabled{false};
    bool printDetections{false};
    std::string outputVideoPath;
    int streamPort{0};
    int streamJpegQuality{72};
    int streamWidth{640};
    int streamHeight{0};
    std::string llmEndpoint{"http://localhost:8000/analyze"};
    long httpTimeoutMs{kDefaultLlmHttpTimeoutMs};
    infra::CanBusConfig can;
    TelemetryConfig telemetry;
};

struct PipelineResult {
    int processedFrames{0};
    std::size_t totalDetections{0};
    std::size_t maxTracks{0};
    std::vector<core::BehaviorEvent> events;
    infra::PerformanceSnapshot performance;
};

class AnalysisPipeline {
public:
    explicit AnalysisPipeline(PipelineConfig config);

    [[nodiscard]] PipelineResult run();

private:
    void enrichWithLlm(std::vector<core::BehaviorEvent>& events, const core::Frame& frame) const;

    PipelineConfig config_;
    std::unique_ptr<core::IDetector> detector_;
    core::SortTracker tracker_;
    core::BehaviorAnalyzer analyzer_;
    infra::FrameCache<core::Frame> cache_;
    infra::PerformanceMonitor monitor_;
    infra::HttpClient httpClient_;
    infra::CanBusClient canBus_;
    std::unique_ptr<TelemetryPublisher> telemetry_;
};

} // namespace vehicle::business
