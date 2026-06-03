#pragma once

#include "vehicle/core/types.hpp"

#include <string>
#include <vector>

#if defined(VEHICLE_WITH_OPENCV)
#include <opencv2/core.hpp>
#endif

namespace vehicle::core {

class Visualizer {
public:
    explicit Visualizer(bool createWindow = true, std::string windowName = "Vehicle Detection");
    ~Visualizer();

#if defined(VEHICLE_WITH_OPENCV)
    [[nodiscard]] cv::Mat render(const Frame& frame,
                                 const std::vector<Track>& tracks,
                                 const std::vector<BehaviorEvent>& events,
                                 double fps) const;
#endif

    [[nodiscard]] bool show(const Frame& frame,
                            const std::vector<Track>& tracks,
                            const std::vector<BehaviorEvent>& events,
                            double fps);

private:
    bool windowCreated_{false};
    int invisibleWindowChecks_{0};
    std::string windowName_;
};

} // namespace vehicle::core
