#include "vehicle/core/video_source.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#include <string>
#include <utility>

namespace vehicle::core {

namespace {

bool isCameraIndex(const std::string& value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

} // namespace

VideoSource::VideoSource(std::string videoPath) : videoPath_(std::move(videoPath))
{
#if defined(VEHICLE_WITH_OPENCV)
    if (isCameraIndex(videoPath_)) {
        const int cameraIndex = std::stoi(videoPath_);
        capture_.open(cameraIndex, cv::CAP_V4L2);
        if (!capture_.isOpened()) {
            capture_.open(cameraIndex);
        }
    } else if (!videoPath_.empty() && fs::exists(videoPath_)) {
        capture_.open(videoPath_);
    }
#endif
}

bool VideoSource::read(Frame& frame)
{
#if defined(VEHICLE_WITH_OPENCV)
    if (capture_.isOpened()) {
        cv::Mat image;
        if (!capture_.read(image) || image.empty()) {
            return false;
        }
        frame = {};
        frame.index = nextFrameIndex_++;
        frame.width = image.cols;
        frame.height = image.rows;
        frame.fps = capture_.get(cv::CAP_PROP_FPS);
        frame.image = std::move(image);
        return true;
    }
#endif

    frame = makeSyntheticFrame();
    nextFrameIndex_ += 1;
    return true;
}

bool VideoSource::usingOpenCv() const
{
#if defined(VEHICLE_WITH_OPENCV)
    return capture_.isOpened();
#else
    return false;
#endif
}

Frame VideoSource::makeSyntheticFrame() const
{
    Frame frame;
    frame.index = nextFrameIndex_;
    frame.fps = 30.0;

    const double approach = static_cast<double>(nextFrameIndex_);
    frame.syntheticDetections.push_back(
        {{545.0, 225.0 + approach * 5.4, 118.0 + approach * 1.5, 78.0 + approach * 1.0},
         0.94F,
         ObjectClass::Car,
         "car"});

    if (nextFrameIndex_ > 8) {
        frame.syntheticDetections.push_back(
            {{250.0 + (nextFrameIndex_ - 8) * 13.0, 380.0, 42.0, 120.0},
             0.89F,
             ObjectClass::Pedestrian,
             "person"});
    }

    if (nextFrameIndex_ > 18) {
        frame.syntheticDetections.push_back(
            {{930.0 - (nextFrameIndex_ - 18) * 12.5, 310.0, 135.0, 86.0},
             0.88F,
             ObjectClass::Truck,
             "truck"});
    }

    return frame;
}

} // namespace vehicle::core
