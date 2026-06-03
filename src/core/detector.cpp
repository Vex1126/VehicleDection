#include "vehicle/core/detector.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#include <memory>
#include <numeric>
#include <unordered_map>
#include <utility>

#if defined(VEHICLE_WITH_OPENCV)
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#endif

#if defined(VEHICLE_WITH_ONNXRUNTIME)
#include <onnxruntime_cxx_api.h>
#endif

namespace vehicle::core {

namespace {

#if defined(VEHICLE_WITH_OPENCV)
ObjectClass cocoToObjectClass(int classId)
{
    switch (classId) {
    case 0:
        return ObjectClass::Pedestrian;
    case 1:
        return ObjectClass::Bicycle;
    case 2:
        return ObjectClass::Car;
    case 3:
        return ObjectClass::Motorcycle;
    case 5:
        return ObjectClass::Bus;
    case 7:
        return ObjectClass::Truck;
    case 9:
        return ObjectClass::TrafficLight;
    default:
        return ObjectClass::Unknown;
    }
}

const char* cocoClassName(int classId)
{
    static constexpr const char* names[] = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
        "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat",
        "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack",
        "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball",
        "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
        "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
        "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake",
        "chair", "couch", "potted plant", "bed", "dining table", "toilet", "tv", "laptop",
        "mouse", "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
        "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier",
        "toothbrush"};
    constexpr int count = static_cast<int>(sizeof(names) / sizeof(names[0]));
    if (classId < 0 || classId >= count) {
        return "unknown";
    }
    return names[classId];
}

struct LetterboxTransform {
    float scale{1.0F};
    float padX{0.0F};
    float padY{0.0F};
    int frameWidth{0};
    int frameHeight{0};
};

cv::Mat letterboxImage(const cv::Mat& image, int inputWidth, int inputHeight, LetterboxTransform& transform)
{
    transform.frameWidth = image.cols;
    transform.frameHeight = image.rows;
    transform.scale = std::min(static_cast<float>(inputWidth) / static_cast<float>(image.cols),
                               static_cast<float>(inputHeight) / static_cast<float>(image.rows));

    const int resizedWidth = std::max(1, static_cast<int>(std::round(image.cols * transform.scale)));
    const int resizedHeight = std::max(1, static_cast<int>(std::round(image.rows * transform.scale)));
    transform.padX = static_cast<float>(inputWidth - resizedWidth) / 2.0F;
    transform.padY = static_cast<float>(inputHeight - resizedHeight) / 2.0F;

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(resizedWidth, resizedHeight));

    cv::Mat padded(inputHeight, inputWidth, image.type(), cv::Scalar(114, 114, 114));
    const cv::Rect target(static_cast<int>(std::round(transform.padX)),
                          static_cast<int>(std::round(transform.padY)),
                          resizedWidth,
                          resizedHeight);
    resized.copyTo(padded(target));
    return padded;
}

std::vector<Detection> decodeYoloOutput(const float* outputData,
                                        int rows,
                                        int attributes,
                                        bool attributesFirst,
                                        const LetterboxTransform& transform,
                                        float scoreThreshold,
                                        float nmsThreshold)
{
    if (outputData == nullptr || rows <= 0 || attributes < 6 || transform.scale <= 0.0F) {
        return {};
    }

    auto valueAt = [=](int row, int attribute) {
        return attributesFirst ? outputData[attribute * rows + row] : outputData[row * attributes + attribute];
    };

    const bool hasObjectness = attributes > 84;
    const cv::Rect frameBounds(0, 0, transform.frameWidth, transform.frameHeight);

    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    std::vector<int> classIds;

    for (int row = 0; row < rows; ++row) {
        const int classOffset = hasObjectness ? 5 : 4;
        const int classCount = attributes - classOffset;
        if (classCount <= 0) {
            continue;
        }

        float bestScore = 0.0F;
        int bestClass = -1;
        for (int classIndex = 0; classIndex < classCount; ++classIndex) {
            const float score = valueAt(row, classOffset + classIndex) *
                                (hasObjectness ? valueAt(row, 4) : 1.0F);
            if (score > bestScore) {
                bestScore = score;
                bestClass = classIndex;
            }
        }

        if (bestScore < scoreThreshold) {
            continue;
        }

        const float cx = (valueAt(row, 0) - transform.padX) / transform.scale;
        const float cy = (valueAt(row, 1) - transform.padY) / transform.scale;
        const float width = valueAt(row, 2) / transform.scale;
        const float height = valueAt(row, 3) / transform.scale;

        cv::Rect box(static_cast<int>(std::round(cx - width / 2.0F)),
                     static_cast<int>(std::round(cy - height / 2.0F)),
                     static_cast<int>(std::round(width)),
                     static_cast<int>(std::round(height)));
        box &= frameBounds;
        if (box.width <= 1 || box.height <= 1) {
            continue;
        }

        boxes.push_back(box);
        scores.push_back(bestScore);
        classIds.push_back(bestClass);
    }

    std::vector<int> kept;
    cv::dnn::NMSBoxes(boxes, scores, scoreThreshold, nmsThreshold, kept);

    std::vector<Detection> detections;
    detections.reserve(kept.size());
    for (const int index : kept) {
        const auto& box = boxes[index];
        detections.push_back({{static_cast<double>(box.x),
                               static_cast<double>(box.y),
                               static_cast<double>(box.width),
                               static_cast<double>(box.height)},
                              scores[index],
                              cocoToObjectClass(classIds[index]),
                              cocoClassName(classIds[index])});
    }

    return detections;
}
#endif

} // namespace

double Rect::area() const
{
    return std::max(0.0, width) * std::max(0.0, height);
}

Point Rect::center() const
{
    return {x + width / 2.0, y + height / 2.0};
}

const char* toString(ObjectClass label)
{
    switch (label) {
    case ObjectClass::Car:
        return "car";
    case ObjectClass::Truck:
        return "truck";
    case ObjectClass::Bus:
        return "bus";
    case ObjectClass::Motorcycle:
        return "motorcycle";
    case ObjectClass::Bicycle:
        return "bicycle";
    case ObjectClass::Pedestrian:
        return "pedestrian";
    case ObjectClass::TrafficLight:
        return "traffic_light";
    case ObjectClass::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* toString(RiskLevel level)
{
    switch (level) {
    case RiskLevel::Low:
        return "low";
    case RiskLevel::Medium:
        return "medium";
    case RiskLevel::High:
        return "high";
    case RiskLevel::Critical:
        return "critical";
    }
    return "low";
}

double intersectionOverUnion(const Rect& lhs, const Rect& rhs)
{
    const double x1 = std::max(lhs.x, rhs.x);
    const double y1 = std::max(lhs.y, rhs.y);
    const double x2 = std::min(lhs.x + lhs.width, rhs.x + rhs.width);
    const double y2 = std::min(lhs.y + lhs.height, rhs.y + rhs.height);

    const double intersectionWidth = std::max(0.0, x2 - x1);
    const double intersectionHeight = std::max(0.0, y2 - y1);
    const double intersection = intersectionWidth * intersectionHeight;
    const double combined = lhs.area() + rhs.area() - intersection;
    if (combined <= 0.0) {
        return 0.0;
    }
    return intersection / combined;
}

SyntheticYoloDetector::SyntheticYoloDetector(std::string modelPath, std::string device)
    : modelPath_(std::move(modelPath)), device_(std::move(device))
{
}

std::vector<Detection> SyntheticYoloDetector::detect(const Frame& frame)
{
    if (!frame.syntheticDetections.empty()) {
        return frame.syntheticDetections;
    }

    const double phase = static_cast<double>(frame.index % 120);
    return {
        Detection{{540.0 + std::sin(phase / 9.0) * 18.0, 280.0 + phase * 0.8, 120.0, 82.0},
                  0.91F,
                  ObjectClass::Car,
                  "car"},
        Detection{{180.0 + phase * 1.2, 365.0, 46.0, 118.0}, 0.87F, ObjectClass::Pedestrian, "person"},
    };
}

#if defined(VEHICLE_WITH_OPENCV)
OpenCvYoloDetector::OpenCvYoloDetector(std::string modelPath, std::string device)
    : modelPath_(std::move(modelPath)), device_(std::move(device))
{
    net_ = cv::dnn::readNetFromONNX(modelPath_);
if (device_ == "gpu" || device_ == "cuda") {
#if defined(CV_DNN_BACKEND_CUDA) && defined(CV_DNN_TARGET_CUDA)
    net_.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
    net_.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
#else
    net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
#endif
} else {
    net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
}
}

std::vector<Detection> OpenCvYoloDetector::detect(const Frame& frame)
{
    if (frame.image.empty()) {
        return frame.syntheticDetections;
    }

    LetterboxTransform transform;
    const cv::Mat inputImage = letterboxImage(frame.image, inputWidth_, inputHeight_, transform);
    cv::Mat blob = cv::dnn::blobFromImage(inputImage,
                                          1.0 / 255.0,
                                          cv::Size(inputWidth_, inputHeight_),
                                          cv::Scalar(),
                                          true,
                                          false);
    net_.setInput(blob);

    std::vector<cv::Mat> outputs;
    net_.forward(outputs, net_.getUnconnectedOutLayersNames());
    if (outputs.empty()) {
        return {};
    }

    cv::Mat output = outputs.front();
    if (output.dims == 3 && output.size[1] < output.size[2]) {
        output = output.reshape(1, output.size[1]);
        cv::transpose(output, output);
    } else if (output.dims == 3) {
        output = output.reshape(1, output.size[1]);
    } else if (output.dims != 2) {
        return {};
    }

    return decodeYoloOutput(output.ptr<float>(),
                            output.rows,
                            output.cols,
                            false,
                            transform,
                            scoreThreshold_,
                            nmsThreshold_);
}
#endif

#if defined(VEHICLE_WITH_ONNXRUNTIME)
class OnnxRuntimeYoloDetector final : public IDetector {
public:
    OnnxRuntimeYoloDetector(std::string modelPath, std::string device)
        : modelPath_(std::move(modelPath)),
          device_(std::move(device)),
          env_(ORT_LOGGING_LEVEL_WARNING, "vehicle-yolo"),
          memoryInfo_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
    {
        sessionOptions_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        if (device_ == "gpu" || device_ == "cuda") {
            try {
                Ort::CUDAProviderOptions cudaOptions;
                cudaOptions.Update({{"device_id", "0"}, {"arena_extend_strategy", "kNextPowerOfTwo"}});
                sessionOptions_.AppendExecutionProvider_CUDA_V2(*cudaOptions);
                std::cerr << "[vehicle] ONNX Runtime CUDA execution provider enabled.\n";
            } catch (const Ort::Exception& error) {
                std::cerr << "[vehicle] ONNX Runtime CUDA provider unavailable: " << error.what()
                          << "; falling back to CPU provider.\n";
            }
        }
        session_ = std::make_unique<Ort::Session>(env_, modelPath_.c_str(), sessionOptions_);

        inputNames_.push_back(session_->GetInputNameAllocated(0, allocator_).get());
        outputNames_.push_back(session_->GetOutputNameAllocated(0, allocator_).get());
        inputNamePtrs_.push_back(inputNames_.back().c_str());
        outputNamePtrs_.push_back(outputNames_.back().c_str());

        const auto inputShape = session_->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (inputShape.size() == 4) {
            if (inputShape[2] > 0) {
                inputHeight_ = static_cast<int>(inputShape[2]);
            }
            if (inputShape[3] > 0) {
                inputWidth_ = static_cast<int>(inputShape[3]);
            }
        }

    }

    [[nodiscard]] std::vector<Detection> detect(const Frame& frame) override
    {
        if (frame.image.empty()) {
            return frame.syntheticDetections;
        }

        LetterboxTransform transform;
        const cv::Mat inputImage = letterboxImage(frame.image, inputWidth_, inputHeight_, transform);
        cv::Mat blob = cv::dnn::blobFromImage(inputImage,
                                              1.0 / 255.0,
                                              cv::Size(inputWidth_, inputHeight_),
                                              cv::Scalar(),
                                              true,
                                              false);
        const auto inputBytes = blob.total();
        std::vector<int64_t> inputShape{1, 3, inputHeight_, inputWidth_};
        auto inputTensor = Ort::Value::CreateTensor<float>(memoryInfo_,
                                                           blob.ptr<float>(),
                                                           inputBytes,
                                                           inputShape.data(),
                                                           inputShape.size());

        auto outputTensors = session_->Run(Ort::RunOptions{nullptr},
                                           inputNamePtrs_.data(),
                                           &inputTensor,
                                           1,
                                           outputNamePtrs_.data(),
                                           outputNamePtrs_.size());
        if (outputTensors.empty() || !outputTensors.front().IsTensor()) {
            return {};
        }

        const auto shape = outputTensors.front().GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 3) {
            return {};
        }

        const bool attributesFirst = shape[1] < shape[2];
        const int attributes = static_cast<int>(attributesFirst ? shape[1] : shape[2]);
        const int rows = static_cast<int>(attributesFirst ? shape[2] : shape[1]);
        const float* outputData = outputTensors.front().GetTensorData<float>();

        return decodeYoloOutput(outputData,
                                rows,
                                attributes,
                                attributesFirst,
                                transform,
                                scoreThreshold_,
                                nmsThreshold_);
    }

private:
    std::string modelPath_;
    std::string device_;
    Ort::Env env_;
    Ort::SessionOptions sessionOptions_;
    Ort::AllocatorWithDefaultOptions allocator_;
    Ort::MemoryInfo memoryInfo_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> inputNames_;
    std::vector<std::string> outputNames_;
    std::vector<const char*> inputNamePtrs_;
    std::vector<const char*> outputNamePtrs_;
    int inputWidth_{640};
    int inputHeight_{640};
    float scoreThreshold_{0.25F};
    float nmsThreshold_{0.45F};
};
#endif

const std::string& SyntheticYoloDetector::modelPath() const
{
    return modelPath_;
}

const std::string& SyntheticYoloDetector::device() const
{
    return device_;
}

std::unique_ptr<IDetector> createDetector(const std::string& modelPath, const std::string& device)
{
#if defined(VEHICLE_WITH_ONNXRUNTIME)
    if (!modelPath.empty() && fs::exists(modelPath)) {
        try {
            std::cerr << "[vehicle] loading YOLO ONNX with ONNX Runtime: " << modelPath << '\n';
            return std::make_unique<OnnxRuntimeYoloDetector>(modelPath, device);
        } catch (const Ort::Exception& error) {
            std::cerr << "[vehicle] ONNX Runtime failed to load YOLO ONNX: " << error.what() << '\n';
        }
    }
#endif
#if defined(VEHICLE_WITH_OPENCV)
    if (!modelPath.empty() && fs::exists(modelPath)) {
        try {
            std::cerr << "[vehicle] loading YOLO ONNX with OpenCV DNN: " << modelPath << '\n';
            return std::make_unique<OpenCvYoloDetector>(modelPath, device);
        } catch (const cv::Exception& error) {
            std::cerr << "[vehicle] OpenCV DNN failed to load YOLO ONNX: " << error.what() << '\n';
            std::cerr << "[vehicle] falling back to SyntheticYoloDetector. Use a newer OpenCV build or "
                         "add ONNX Runtime for real YOLO11 inference.\n";
        }
    }
#endif
    return std::make_unique<SyntheticYoloDetector>(modelPath, device);
}

} // namespace vehicle::core
