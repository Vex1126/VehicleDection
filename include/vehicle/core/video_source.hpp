#pragma once

#include "vehicle/core/types.hpp"

#include <string>

#if defined(VEHICLE_WITH_OPENCV)
#include <opencv2/videoio.hpp>
#endif

namespace vehicle::core {

class VideoSource {
public:
    explicit VideoSource(std::string videoPath);

    [[nodiscard]] bool read(Frame& frame);
    [[nodiscard]] bool usingOpenCv() const;

private:
    [[nodiscard]] Frame makeSyntheticFrame() const;

    std::string videoPath_;
    int nextFrameIndex_{0};
#if defined(VEHICLE_WITH_OPENCV)
    cv::VideoCapture capture_;
#endif
};

} // namespace vehicle::core
