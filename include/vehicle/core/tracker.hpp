#pragma once

#include "vehicle/core/types.hpp"

#include <vector>

namespace vehicle::core {

class SortTracker {
public:
    explicit SortTracker(double minIou = 0.25, int maxMissedFrames = 5);

    [[nodiscard]] std::vector<Track> update(const std::vector<Detection>& detections);

private:
    int nextId_{1};
    double minIou_{0.25};
    int maxMissedFrames_{5};
    std::vector<Track> tracks_;
};

} // namespace vehicle::core
