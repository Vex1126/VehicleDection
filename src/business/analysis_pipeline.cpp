#include "vehicle/business/analysis_pipeline.hpp"

#include "vehicle/infra/logger.hpp"
#include "vehicle/infra/mjpeg_streamer.hpp"

#include <cctype>
#include <chrono>
#include <exception>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#if defined(VEHICLE_WITH_OPENCV)
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#endif

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
                escaped << digits[(static_cast<unsigned char>(ch) >> 4) & 0x0F]
                        << digits[static_cast<unsigned char>(ch) & 0x0F];
            } else {
                escaped << ch;
            }
        }
    }
    return escaped.str();
}

std::string base64Encode(const std::vector<unsigned char>& bytes)
{
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);

    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned int b0 = bytes[i];
        const unsigned int b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const unsigned int b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        const unsigned int triple = (b0 << 16U) | (b1 << 8U) | b2;

        encoded.push_back(alphabet[(triple >> 18U) & 0x3FU]);
        encoded.push_back(alphabet[(triple >> 12U) & 0x3FU]);
        encoded.push_back(i + 1 < bytes.size() ? alphabet[(triple >> 6U) & 0x3FU] : '=');
        encoded.push_back(i + 2 < bytes.size() ? alphabet[triple & 0x3FU] : '=');
    }

    return encoded;
}

#if defined(VEHICLE_WITH_OPENCV)
std::optional<std::string> encodeFrameJpegBase64(const core::Frame& frame)
{
    if (frame.image.empty()) {
        return std::nullopt;
    }

    std::vector<unsigned char> encoded;
    const std::vector<int> params{cv::IMWRITE_JPEG_QUALITY, 80};
    if (!cv::imencode(".jpg", frame.image, encoded, params) || encoded.empty()) {
        return std::nullopt;
    }
    return base64Encode(encoded);
}

cv::Mat resizeForStream(const cv::Mat& image, int targetWidth, int targetHeight)
{
    if (image.empty()) {
        return {};
    }
    if (targetWidth <= 0 && targetHeight <= 0) {
        return image;
    }
    if (targetHeight <= 0) {
        if (targetWidth > 0 && image.cols > targetWidth) {
            const double scale = static_cast<double>(targetWidth) / static_cast<double>(image.cols);
            cv::Mat resized;
            cv::resize(image, resized, cv::Size(targetWidth, std::max(1, static_cast<int>(image.rows * scale))));
            return resized;
        }
        return image;
    }

    if (targetWidth <= 0) {
        const double scale = static_cast<double>(targetHeight) / static_cast<double>(image.rows);
        targetWidth = std::max(1, static_cast<int>(std::lround(image.cols * scale)));
    }

    const double scale = std::min(static_cast<double>(targetWidth) / static_cast<double>(image.cols),
                                  static_cast<double>(targetHeight) / static_cast<double>(image.rows));
    const cv::Size scaledSize(std::max(1, static_cast<int>(std::lround(image.cols * scale))),
                              std::max(1, static_cast<int>(std::lround(image.rows * scale))));

    cv::Mat resized;
    if (scaledSize.width == image.cols && scaledSize.height == image.rows) {
        resized = image;
    } else {
        cv::resize(image, resized, scaledSize);
    }

    cv::Mat canvas(targetHeight, targetWidth, image.type(), cv::Scalar::all(0));
    const cv::Rect target((targetWidth - scaledSize.width) / 2,
                          (targetHeight - scaledSize.height) / 2,
                          scaledSize.width,
                          scaledSize.height);
    resized.copyTo(canvas(target));
    return canvas;
}
#endif

std::string trim(const std::string& value)
{
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::optional<core::RiskLevel> parseRiskLevel(std::string value)
{
    value = trim(value);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (value == "low") {
        return core::RiskLevel::Low;
    }
    if (value == "medium") {
        return core::RiskLevel::Medium;
    }
    if (value == "high") {
        return core::RiskLevel::High;
    }
    if (value == "critical") {
        return core::RiskLevel::Critical;
    }
    return std::nullopt;
}

std::optional<std::size_t> findMatchingBracket(const std::string& text, std::size_t openPos)
{
    if (openPos >= text.size()) {
        return std::nullopt;
    }

    const char open = text[openPos];
    const char close = open == '[' ? ']' : '}';
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = openPos; i < text.size(); ++i) {
        const char ch = text[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
        } else if (ch == open) {
            ++depth;
        } else if (ch == close) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> extractEventsArray(const std::string& json)
{
    const auto key = json.find("\"events\"");
    if (key == std::string::npos) {
        return std::nullopt;
    }
    const auto colon = json.find(':', key);
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    const auto open = json.find('[', colon);
    if (open == std::string::npos) {
        return std::nullopt;
    }
    const auto close = findMatchingBracket(json, open);
    if (!close.has_value()) {
        return std::nullopt;
    }
    return json.substr(open + 1, *close - open - 1);
}

std::vector<std::string> splitTopLevelObjects(const std::string& arrayBody)
{
    std::vector<std::string> objects;
    std::size_t pos = 0;
    while (pos < arrayBody.size()) {
        const auto open = arrayBody.find('{', pos);
        if (open == std::string::npos) {
            break;
        }
        const auto close = findMatchingBracket(arrayBody, open);
        if (!close.has_value()) {
            break;
        }
        objects.push_back(arrayBody.substr(open, *close - open + 1));
        pos = *close + 1;
    }
    return objects;
}

std::optional<std::string> extractJsonStringField(const std::string& object, const std::string& field)
{
    const auto key = object.find("\"" + field + "\"");
    if (key == std::string::npos) {
        return std::nullopt;
    }
    const auto colon = object.find(':', key);
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    const auto quote = object.find('"', colon + 1);
    if (quote == std::string::npos) {
        return std::nullopt;
    }

    std::ostringstream value;
    bool escaped = false;
    for (std::size_t i = quote + 1; i < object.size(); ++i) {
        const char ch = object[i];
        if (escaped) {
            switch (ch) {
            case '"':
            case '\\':
            case '/':
                value << ch;
                break;
            case 'n':
                value << '\n';
                break;
            case 'r':
                value << '\r';
                break;
            case 't':
                value << '\t';
                break;
            default:
                value << ch;
                break;
            }
            escaped = false;
        } else if (ch == '\\') {
            escaped = true;
        } else if (ch == '"') {
            return value.str();
        } else {
            value << ch;
        }
    }
    return std::nullopt;
}

std::optional<int> extractJsonIntField(const std::string& object, const std::string& field)
{
    const auto key = object.find("\"" + field + "\"");
    if (key == std::string::npos) {
        return std::nullopt;
    }
    const auto colon = object.find(':', key);
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::size_t begin = colon + 1;
    while (begin < object.size() && std::isspace(static_cast<unsigned char>(object[begin])) != 0) {
        ++begin;
    }
    std::size_t end = begin;
    if (end < object.size() && object[end] == '-') {
        ++end;
    }
    const std::size_t digitsBegin = end;
    while (end < object.size() && std::isdigit(static_cast<unsigned char>(object[end])) != 0) {
        ++end;
    }
    if (end == digitsBegin) {
        return std::nullopt;
    }
    return std::stoi(object.substr(begin, end - begin));
}

std::optional<bool> extractJsonBoolField(const std::string& object, const std::string& field)
{
    const auto key = object.find("\"" + field + "\"");
    if (key == std::string::npos) {
        return std::nullopt;
    }
    const auto colon = object.find(':', key);
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    std::size_t begin = colon + 1;
    while (begin < object.size() && std::isspace(static_cast<unsigned char>(object[begin])) != 0) {
        ++begin;
    }
    if (object.compare(begin, 4, "true") == 0) {
        return true;
    }
    if (object.compare(begin, 5, "false") == 0) {
        return false;
    }
    return std::nullopt;
}

void applyLlmReview(std::vector<core::BehaviorEvent>& events, const std::string& responseBody)
{
    const auto eventArray = extractEventsArray(responseBody);
    if (!eventArray.has_value()) {
        return;
    }

    for (const auto& object : splitTopLevelObjects(*eventArray)) {
        const auto trackId = extractJsonIntField(object, "trackId");
        const auto behavior = extractJsonStringField(object, "behavior");
        const auto riskText = extractJsonStringField(object, "risk");
        const auto accepted = extractJsonBoolField(object, "accepted").value_or(true);
        if (!trackId.has_value() || !behavior.has_value()) {
            continue;
        }
        const auto risk = accepted ? (riskText.has_value() ? parseRiskLevel(*riskText) : std::nullopt)
                                   : std::optional<core::RiskLevel>{core::RiskLevel::Low};
        if (!risk.has_value()) {
            continue;
        }

        const auto reason = extractJsonStringField(object, "evidence")
                                .value_or(extractJsonStringField(object, "reason").value_or(""));
        for (auto& event : events) {
            if (event.trackId != *trackId || event.behavior != *behavior) {
                continue;
            }
            const auto oldRisk = event.risk;
            event.risk = *risk;
            event.evidence += "; llm_review=";
            event.evidence += core::toString(oldRisk);
            event.evidence += "->";
            event.evidence += core::toString(event.risk);
            if (!accepted) {
                event.evidence += ", accepted=false";
            }
            if (!reason.empty()) {
                event.evidence += ", reason=";
                event.evidence += reason;
            }
        }
    }
}

} // namespace

AnalysisPipeline::AnalysisPipeline(PipelineConfig config)
    : config_(std::move(config)),
      detector_(core::createDetector(config_.modelPath, config_.device)),
      cache_(config_.cacheCapacity),
      canBus_(config_.can)
{
}

PipelineResult AnalysisPipeline::run()
{
    PipelineResult result;
    std::unique_ptr<core::Visualizer> visualizer;
    if (config_.displayEnabled || !config_.outputVideoPath.empty() || config_.streamPort > 0) {
        visualizer = std::make_unique<core::Visualizer>(config_.displayEnabled);
    }
#if defined(VEHICLE_WITH_OPENCV)
    cv::VideoWriter outputVideo;
    std::unique_ptr<infra::MjpegStreamer> streamer;
#endif
    double outputFps = 20.0;

    infra::Logger::instance().log(infra::LogLevel::Info,
                                  "starting pipeline with model=" + config_.modelPath +
                                      ", device=" + config_.device);

#if defined(VEHICLE_WITH_OPENCV)
    if (config_.streamPort > 0) {
        streamer = std::make_unique<infra::MjpegStreamer>();
        std::string error;
        if (!streamer->start(config_.streamPort, &error)) {
            infra::Logger::instance().log(infra::LogLevel::Warning, "failed to start MJPEG stream: " + error);
            streamer.reset();
        } else {
            infra::Logger::instance().log(infra::LogLevel::Info, "MJPEG stream available at port " +
                                                                      std::to_string(config_.streamPort));
        }
    }
#endif

    if (config_.can.enabled) {
        const bool canStarted = canBus_.start();
        (void)canStarted;
    }
    if (config_.telemetry.enabled) {
        telemetry_ = std::make_unique<TelemetryPublisher>(config_.telemetry);
        const bool telemetryStarted = telemetry_->start();
        (void)telemetryStarted;
    }

    infra::BlockingQueue<core::Frame> queue(std::max<std::size_t>(1, config_.frameQueueSize));
    std::exception_ptr captureError;
    std::thread captureThread([this, &queue, &captureError] {
        try {
            core::VideoSource source(config_.videoPath);
            infra::Logger::instance().log(
                infra::LogLevel::Info,
                source.usingOpenCv() ? "reading frames with OpenCV VideoCapture" : "using synthetic frame source");

            int processed = 0;
            while (config_.frameCount <= 0 || processed < config_.frameCount) {
                core::Frame frame;
                if (!source.read(frame)) {
                    break;
                }
                if (!queue.push(std::move(frame))) {
                    break;
                }
                processed += 1;
            }
        } catch (...) {
            captureError = std::current_exception();
        }
        queue.close();
    });

    while (auto frameOpt = queue.pop()) {
        const auto begin = std::chrono::steady_clock::now();
        auto frame = std::move(*frameOpt);
        cache_.push(frame);

        auto detections = detector_->detect(frame);
        auto tracks = tracker_.update(detections);
        const auto vehicleState = canBus_.snapshot();
        auto events = analyzer_.analyze(tracks, frame, vehicleState);
        if (config_.enableLlmAnalysis) {
            enrichWithLlm(events, frame);
        }

        result.events.insert(result.events.end(), events.begin(), events.end());
        result.processedFrames += 1;
        result.totalDetections += detections.size();
        result.maxTracks = std::max(result.maxTracks, tracks.size());

        const auto elapsedMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        monitor_.frameProcessed(elapsedMs);
        const auto fps = monitor_.snapshot().fps;

        if (telemetry_) {
            telemetry_->publishFrameSummary(frame.index, detections.size(), tracks.size(), fps, events, vehicleState);
        }

        if (config_.printDetections) {
            std::cout << "frame=" << frame.index << ", detections=" << detections.size()
                      << ", tracks=" << tracks.size() << ", events=" << events.size() << '\n';
        }

        if (visualizer) {
            if (config_.displayEnabled && !visualizer->show(frame, tracks, events, fps)) {
                queue.close();
                break;
            }
#if defined(VEHICLE_WITH_OPENCV)
            if ((!config_.outputVideoPath.empty() || streamer) && !frame.image.empty()) {
                const auto annotated = visualizer->render(frame, tracks, events, fps);
                if (!annotated.empty()) {
                    if (!config_.outputVideoPath.empty()) {
                        if (!outputVideo.isOpened()) {
                            outputFps = frame.fps > 1.0 ? frame.fps : 20.0;
                            const int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
                            outputVideo.open(config_.outputVideoPath, fourcc, outputFps, annotated.size());
                        }
                        if (outputVideo.isOpened()) {
                            outputVideo.write(annotated);
                        }
                    }
                    if (streamer) {
                        cv::Mat streamed = resizeForStream(annotated, config_.streamWidth, config_.streamHeight);
                        std::vector<unsigned char> encoded;
                        const std::vector<int> params{
                            cv::IMWRITE_JPEG_QUALITY, std::clamp(config_.streamJpegQuality, 1, 100)};
                        if (cv::imencode(".jpg", streamed, encoded, params)) {
                            streamer->publish(std::move(encoded));
                        }
                    }
                }
            }
#endif
        }
    }

    if (captureThread.joinable()) {
        captureThread.join();
    }
    if (captureError) {
        std::rethrow_exception(captureError);
    }

#if defined(VEHICLE_WITH_OPENCV)
    if (streamer) {
        streamer->stop();
    }
#endif

    result.performance = monitor_.snapshot();
    if (telemetry_) {
        telemetry_->publishStatus(result.performance, result.processedFrames);
        telemetry_->stop();
        telemetry_.reset();
    }
    canBus_.stop();
    return result;
}

void AnalysisPipeline::enrichWithLlm(std::vector<core::BehaviorEvent>& events, const core::Frame& frame) const
{
    if (events.empty()) {
        return;
    }

    std::ostringstream payload;
    payload << "{\"frame\":{\"index\":" << frame.index << ",\"width\":" << frame.width
            << ",\"height\":" << frame.height;
#if defined(VEHICLE_WITH_OPENCV)
    const auto frameImage = encodeFrameJpegBase64(frame);
    if (frameImage.has_value()) {
        payload << ",\"image\":{\"mime\":\"image/jpeg\",\"data\":\"" << *frameImage << "\"}";
    }
#endif
    payload << "},\"events\":[";
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (i != 0) {
            payload << ',';
        }
        payload << "{\"trackId\":" << events[i].trackId << ",\"behavior\":\""
                << escapeJsonString(events[i].behavior) << "\",\"risk\":\"" << core::toString(events[i].risk)
                << "\",\"evidence\":\"" << escapeJsonString(events[i].evidence) << "\",\"objectClass\":\""
                << escapeJsonString(events[i].objectClass) << "\",\"bbox\":{\"x\":" << events[i].targetBox.x
                << ",\"y\":" << events[i].targetBox.y << ",\"width\":" << events[i].targetBox.width
                << ",\"height\":" << events[i].targetBox.height << "}}";
    }
    payload << "]}";

    const auto response = httpClient_.postJson(config_.llmEndpoint, payload.str(), config_.httpTimeoutMs);
    if (response.statusCode >= 200 && response.statusCode < 300) {
        applyLlmReview(events, response.body);
    } else if (!response.error.empty()) {
        infra::Logger::instance().log(infra::LogLevel::Warning,
                                      "LLM HTTP analysis failed: " + response.error);
    }
}

} // namespace vehicle::business
