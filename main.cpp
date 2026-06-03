#include "vehicle/business/analysis_pipeline.hpp"
#include "vehicle/core/types.hpp"
#include "vehicle/infra/logger.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

namespace {

std::unordered_map<std::string, std::string> parseArgs(int argc, char** argv)
{
    std::unordered_map<std::string, std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        if (key.rfind("--", 0) != 0) {
            continue;
        }
        if (key == "--llm" || key == "--display" || key == "--print-detections" ||
            key == "--can" || key == "--mqtt" || key == "--mqtt-retain") {
            args[key] = "true";
            continue;
        }
        if (i + 1 < argc) {
            args[key] = argv[++i];
        }
    }
    return args;
}

int intArg(const std::unordered_map<std::string, std::string>& args, const std::string& key, int fallback)
{
    const auto found = args.find(key);
    if (found == args.end()) {
        return fallback;
    }
    return std::atoi(found->second.c_str());
}

std::string stringArg(const std::unordered_map<std::string, std::string>& args,
                      const std::string& key,
                      const std::string& fallback)
{
    const auto found = args.find(key);
    return found == args.end() ? fallback : found->second;
}

} // namespace

int main(int argc, char** argv)
{
    const auto args = parseArgs(argc, argv);

    vehicle::business::PipelineConfig config;
    config.frameCount = intArg(args, "--frames", 60);
    config.frameQueueSize = static_cast<std::size_t>(intArg(args, "--queue-size", 8));
    config.cacheCapacity = static_cast<std::size_t>(intArg(args, "--cache-size", 30));
    config.httpTimeoutMs = intArg(args, "--http-timeout-ms", static_cast<int>(config.httpTimeoutMs));
    config.modelPath = stringArg(args, "--model", config.modelPath);
    config.videoPath = stringArg(args, "--video", config.videoPath);
    config.device = stringArg(args, "--device", config.device);
    config.enableLlmAnalysis = args.count("--llm") != 0U;
    config.displayEnabled = args.count("--display") != 0U;
    config.printDetections = args.count("--print-detections") != 0U;
    config.outputVideoPath = stringArg(args, "--output-video", "");
    config.streamPort = intArg(args, "--stream-port", 0);
    config.streamJpegQuality = intArg(args, "--stream-quality", config.streamJpegQuality);
    config.streamWidth = intArg(args, "--stream-width", config.streamWidth);
    config.streamHeight = intArg(args, "--stream-height", config.streamHeight);
    config.llmEndpoint = stringArg(args, "--llm-endpoint", config.llmEndpoint);

    config.can.enabled = args.count("--can") != 0U;
    config.can.interfaceName = stringArg(args, "--can-interface", config.can.interfaceName);
    config.can.receiveTimeoutMs = intArg(args, "--can-timeout-ms", config.can.receiveTimeoutMs);

    config.telemetry.enabled = args.count("--mqtt") != 0U;
    config.telemetry.topicPrefix = stringArg(args, "--mqtt-topic-prefix", config.telemetry.topicPrefix);
    config.telemetry.queueCapacity = static_cast<std::size_t>(intArg(args, "--mqtt-queue-size", 128));
    config.telemetry.mqtt.enabled = config.telemetry.enabled;
    config.telemetry.mqtt.broker = stringArg(args, "--mqtt-broker", config.telemetry.mqtt.broker);
    config.telemetry.mqtt.clientId = stringArg(args, "--mqtt-client-id", config.telemetry.mqtt.clientId);
    config.telemetry.mqtt.username = stringArg(args, "--mqtt-username", config.telemetry.mqtt.username);
    config.telemetry.mqtt.password = stringArg(args, "--mqtt-password", config.telemetry.mqtt.password);
    config.telemetry.mqtt.qos = intArg(args, "--mqtt-qos", config.telemetry.mqtt.qos);
    config.telemetry.mqtt.retain = args.count("--mqtt-retain") != 0U;
    config.telemetry.mqtt.keepAliveSeconds = intArg(args, "--mqtt-keepalive", config.telemetry.mqtt.keepAliveSeconds);

    const auto logLevel = vehicle::infra::parseLogLevel(stringArg(args, "--log-level", "info"));
    const auto logFile = stringArg(args, "--log-file", "");
    vehicle::infra::Logger::instance().configure(logLevel, logFile, true);

    vehicle::business::AnalysisPipeline pipeline(config);
    const auto result = pipeline.run();

    std::cout << "\nVehicle target detection and behavior analysis result\n";
    std::cout << "processed_frames=" << result.processedFrames << '\n';
    std::cout << "detections=" << result.totalDetections << '\n';
    std::cout << "max_tracks=" << result.maxTracks << '\n';
    std::cout << "events=" << result.events.size() << '\n';
    std::cout << "fps=" << result.performance.fps
              << ", avg_frame_ms=" << result.performance.averageFrameMs
              << ", cpu_percent=" << result.performance.cpuPercent
              << "%, rss_bytes=" << result.performance.memoryBytes << '\n';

    for (const auto& event : result.events) {
        std::cout << "track=" << event.trackId << ", behavior=" << event.behavior
                  << ", risk=" << vehicle::core::toString(event.risk)
                  << ", evidence=\"" << event.evidence << "\"\n";
    }

    return 0;
}
