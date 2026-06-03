#include "vehicle/core/visualizer.hpp"

#include <map>
#include <sstream>
#include <utility>

#if defined(VEHICLE_WITH_OPENCV)
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#endif

namespace vehicle::core {

Visualizer::Visualizer(bool createWindow, std::string windowName)
    : windowCreated_(createWindow),
      windowName_(std::move(windowName))
{
#if defined(VEHICLE_WITH_OPENCV)
    if (windowCreated_) {
        cv::namedWindow(windowName_, cv::WINDOW_NORMAL);
    }
#else
    (void)createWindow;
#endif
}

Visualizer::~Visualizer()
{
#if defined(VEHICLE_WITH_OPENCV)
    if (windowCreated_) {
        cv::destroyWindow(windowName_);
    }
#endif
}

#if defined(VEHICLE_WITH_OPENCV)
cv::Mat Visualizer::render(const Frame& frame,
                           const std::vector<Track>& tracks,
                           const std::vector<BehaviorEvent>& events,
                           double fps) const
{
    if (frame.image.empty()) {
        return {};
    }

    std::map<int, RiskLevel> riskByTrack;
    std::map<int, std::string> behaviorByTrack;
    for (const auto& event : events) {
        const auto existing = riskByTrack.find(event.trackId);
        if (existing == riskByTrack.end() || static_cast<int>(event.risk) > static_cast<int>(existing->second)) {
            riskByTrack[event.trackId] = event.risk;
            behaviorByTrack[event.trackId] = event.behavior;
        }
    }

    cv::Mat canvas = frame.image.clone();
    for (const auto& track : tracks) {
        const auto risk = riskByTrack.count(track.id) != 0 ? riskByTrack[track.id] : RiskLevel::Low;
        const cv::Scalar color = risk == RiskLevel::Critical
                                     ? cv::Scalar(0, 0, 255)
                                     : risk == RiskLevel::High ? cv::Scalar(0, 128, 255)
                                                               : risk == RiskLevel::Medium ? cv::Scalar(0, 255, 255)
                                                                                           : cv::Scalar(0, 220, 0);
        const cv::Rect rect(static_cast<int>(track.detection.box.x),
                            static_cast<int>(track.detection.box.y),
                            static_cast<int>(track.detection.box.width),
                            static_cast<int>(track.detection.box.height));
        cv::rectangle(canvas, rect, color, 2);

        std::ostringstream label;
        label << "#" << track.id << " "
              << (track.detection.className.empty() ? toString(track.detection.label) : track.detection.className)
              << " "
              << static_cast<int>(track.detection.confidence * 100.0F) << "%";
        if (behaviorByTrack.count(track.id) != 0) {
            label << " " << behaviorByTrack[track.id];
        }

        const int y = std::max(20, rect.y - 6);
        cv::putText(canvas, label.str(), {rect.x, y}, cv::FONT_HERSHEY_SIMPLEX, 0.55, color, 2);

        for (std::size_t i = 1; i < track.trajectory.size(); ++i) {
            cv::line(canvas,
                     {static_cast<int>(track.trajectory[i - 1].x), static_cast<int>(track.trajectory[i - 1].y)},
                     {static_cast<int>(track.trajectory[i].x), static_cast<int>(track.trajectory[i].y)},
                     color,
                     2);
        }
    }

    std::ostringstream status;
    status << "FPS " << static_cast<int>(fps) << " | tracks " << tracks.size() << " | events " << events.size();
    cv::putText(canvas, status.str(), {18, 32}, cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 255, 255), 2);

    return canvas;
}
#endif

bool Visualizer::show(const Frame& frame,
                      const std::vector<Track>& tracks,
                      const std::vector<BehaviorEvent>& events,
                      double fps)
{
#if defined(VEHICLE_WITH_OPENCV)
    if (!windowCreated_) {
        return true;
    }
    const auto canvas = render(frame, tracks, events, fps);
    if (canvas.empty()) {
        return true;
    }
    cv::imshow(windowName_, canvas);
    const int key = cv::waitKey(10);
    if (key == 27 || key == 'q' || key == 'Q') {
        return false;
    }

    const double visible = cv::getWindowProperty(windowName_, cv::WND_PROP_VISIBLE);
    if (visible == 0.0) {
        invisibleWindowChecks_ += 1;
        return invisibleWindowChecks_ <= 5;
    }
    invisibleWindowChecks_ = 0;
    return true;
#else
    (void)frame;
    (void)tracks;
    (void)events;
    (void)fps;
    return true;
#endif
}

} // namespace vehicle::core
