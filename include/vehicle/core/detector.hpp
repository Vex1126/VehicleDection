#pragma once

#include "vehicle/core/types.hpp"

#include <memory>
#include <string>
#include <vector>

#if defined(VEHICLE_WITH_OPENCV)
#include <opencv2/dnn.hpp>
#endif

namespace vehicle::core {

class IDetector {
public:
    virtual ~IDetector() = default;
    [[nodiscard]] virtual std::vector<Detection> detect(const Frame& frame) = 0;
};

class SyntheticYoloDetector final : public IDetector {
public:
    SyntheticYoloDetector(std::string modelPath, std::string device);

    [[nodiscard]] std::vector<Detection> detect(const Frame& frame) override;
    [[nodiscard]] const std::string& modelPath() const;
    [[nodiscard]] const std::string& device() const;

private:
    std::string modelPath_;
    std::string device_;
};

#if defined(VEHICLE_WITH_OPENCV)
class OpenCvYoloDetector final : public IDetector {
public:
    OpenCvYoloDetector(std::string modelPath, std::string device);

    [[nodiscard]] std::vector<Detection> detect(const Frame& frame) override;

private:
    std::string modelPath_;
    std::string device_;
    cv::dnn::Net net_;
    int inputWidth_{640};
    int inputHeight_{640};
    float scoreThreshold_{0.25F};
    float nmsThreshold_{0.45F};
};
#endif

[[nodiscard]] std::unique_ptr<IDetector> createDetector(const std::string& modelPath,
                                                        const std::string& device);

} // namespace vehicle::core
