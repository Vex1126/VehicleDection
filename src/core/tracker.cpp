#include "vehicle/core/tracker.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace vehicle::core {

namespace {

constexpr double kProcessNoise = 0.03;
constexpr double kMeasurementNoise = 5.0;

std::size_t idx(int row, int col)
{
    return static_cast<std::size_t>(row * 4 + col);
}

Rect boxAtCenter(Rect box, Point center)
{
    box.x = center.x - box.width / 2.0;
    box.y = center.y - box.height / 2.0;
    return box;
}

void initializeKalman(Track& track, Point measurement)
{
    track.kalmanPosition = measurement;
    track.kalmanVelocity = {0.0, 0.0};
    track.kalmanCovariance.fill(0.0);
    track.kalmanCovariance[idx(0, 0)] = 10.0;
    track.kalmanCovariance[idx(1, 1)] = 10.0;
    track.kalmanCovariance[idx(2, 2)] = 100.0;
    track.kalmanCovariance[idx(3, 3)] = 100.0;
    track.kalmanInitialized = true;
}

Point predictKalman(Track& track)
{
    if (!track.kalmanInitialized) {
        initializeKalman(track, track.detection.box.center());
    }

    track.kalmanPosition.x += track.kalmanVelocity.x;
    track.kalmanPosition.y += track.kalmanVelocity.y;

    const auto previous = track.kalmanCovariance;
    auto& p = track.kalmanCovariance;
    p[idx(0, 0)] = previous[idx(0, 0)] + previous[idx(2, 0)] + previous[idx(0, 2)] + previous[idx(2, 2)] + kProcessNoise;
    p[idx(0, 1)] = previous[idx(0, 1)] + previous[idx(2, 1)] + previous[idx(0, 3)] + previous[idx(2, 3)];
    p[idx(0, 2)] = previous[idx(0, 2)] + previous[idx(2, 2)];
    p[idx(0, 3)] = previous[idx(0, 3)] + previous[idx(2, 3)];

    p[idx(1, 0)] = previous[idx(1, 0)] + previous[idx(3, 0)] + previous[idx(1, 2)] + previous[idx(3, 2)];
    p[idx(1, 1)] = previous[idx(1, 1)] + previous[idx(3, 1)] + previous[idx(1, 3)] + previous[idx(3, 3)] + kProcessNoise;
    p[idx(1, 2)] = previous[idx(1, 2)] + previous[idx(3, 2)];
    p[idx(1, 3)] = previous[idx(1, 3)] + previous[idx(3, 3)];

    p[idx(2, 0)] = previous[idx(2, 0)] + previous[idx(2, 2)];
    p[idx(2, 1)] = previous[idx(2, 1)] + previous[idx(2, 3)];
    p[idx(2, 2)] = previous[idx(2, 2)] + kProcessNoise;
    p[idx(2, 3)] = previous[idx(2, 3)];

    p[idx(3, 0)] = previous[idx(3, 0)] + previous[idx(3, 2)];
    p[idx(3, 1)] = previous[idx(3, 1)] + previous[idx(3, 3)];
    p[idx(3, 2)] = previous[idx(3, 2)];
    p[idx(3, 3)] = previous[idx(3, 3)] + kProcessNoise;

    return track.kalmanPosition;
}

Point updateKalman(Track& track, Point measurement)
{
    const double residualX = measurement.x - track.kalmanPosition.x;
    const double residualY = measurement.y - track.kalmanPosition.y;

    auto& p = track.kalmanCovariance;
    const double s00 = p[idx(0, 0)] + kMeasurementNoise;
    const double s01 = p[idx(0, 1)];
    const double s10 = p[idx(1, 0)];
    const double s11 = p[idx(1, 1)] + kMeasurementNoise;
    const double determinant = s00 * s11 - s01 * s10;
    if (std::abs(determinant) < 1e-9) {
        return track.kalmanPosition;
    }

    const double inv00 = s11 / determinant;
    const double inv01 = -s01 / determinant;
    const double inv10 = -s10 / determinant;
    const double inv11 = s00 / determinant;

    double gain[4][2]{};
    for (int row = 0; row < 4; ++row) {
        gain[row][0] = p[idx(row, 0)] * inv00 + p[idx(row, 1)] * inv10;
        gain[row][1] = p[idx(row, 0)] * inv01 + p[idx(row, 1)] * inv11;
    }

    track.kalmanPosition.x += gain[0][0] * residualX + gain[0][1] * residualY;
    track.kalmanPosition.y += gain[1][0] * residualX + gain[1][1] * residualY;
    track.kalmanVelocity.x += gain[2][0] * residualX + gain[2][1] * residualY;
    track.kalmanVelocity.y += gain[3][0] * residualX + gain[3][1] * residualY;

    const auto previous = p;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            p[idx(row, col)] = previous[idx(row, col)] - gain[row][0] * previous[idx(0, col)] -
                               gain[row][1] * previous[idx(1, col)];
        }
    }

    return track.kalmanPosition;
}

} // namespace

SortTracker::SortTracker(double minIou, int maxMissedFrames)
    : minIou_(minIou), maxMissedFrames_(maxMissedFrames)
{
}

std::vector<Track> SortTracker::update(const std::vector<Detection>& detections)
{
    std::unordered_set<std::size_t> usedDetections;

    for (auto& track : tracks_) {
        const Point predictedCenter = predictKalman(track);
        track.detection.box = boxAtCenter(track.detection.box, predictedCenter);

        double bestIou = minIou_;
        std::size_t bestIndex = std::numeric_limits<std::size_t>::max();

        for (std::size_t i = 0; i < detections.size(); ++i) {
            if (usedDetections.count(i) != 0U) {
                continue;
            }
            const double iou = intersectionOverUnion(track.detection.box, detections[i].box);
            if (iou > bestIou) {
                bestIou = iou;
                bestIndex = i;
            }
        }

        if (bestIndex == std::numeric_limits<std::size_t>::max()) {
            track.missedFrames += 1;
            track.velocity = track.kalmanVelocity;
            continue;
        }

        const Point previousCenter = track.detection.box.center();
        const Point filteredCenter = updateKalman(track, detections[bestIndex].box.center());
        Detection smoothed = detections[bestIndex];
        constexpr double alpha = 0.72;
        smoothed.box.width = alpha * detections[bestIndex].box.width + (1.0 - alpha) * track.detection.box.width;
        smoothed.box.height = alpha * detections[bestIndex].box.height + (1.0 - alpha) * track.detection.box.height;
        smoothed.box = boxAtCenter(smoothed.box, filteredCenter);

        track.detection = smoothed;
        const Point currentCenter = track.detection.box.center();
        track.velocity = track.kalmanVelocity;
        if (std::abs(track.velocity.x) < 1e-6 && std::abs(track.velocity.y) < 1e-6) {
            track.velocity = {currentCenter.x - previousCenter.x, currentCenter.y - previousCenter.y};
        }
        track.age += 1;
        track.missedFrames = 0;
        track.trajectory.push_back(currentCenter);
        if (track.trajectory.size() > 20) {
            track.trajectory.pop_front();
        }
        usedDetections.insert(bestIndex);
    }

    for (std::size_t i = 0; i < detections.size(); ++i) {
        if (usedDetections.count(i) != 0U) {
            continue;
        }
        Track track;
        track.id = nextId_++;
        track.detection = detections[i];
        initializeKalman(track, detections[i].box.center());
        track.trajectory.push_back(detections[i].box.center());
        tracks_.push_back(track);
    }

    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [this](const Track& track) {
                      return track.missedFrames > maxMissedFrames_;
                  }),
                  tracks_.end());

    std::vector<Track> visibleTracks;
    std::copy_if(tracks_.begin(), tracks_.end(), std::back_inserter(visibleTracks), [](const Track& track) {
        return track.missedFrames == 0;
    });
    return visibleTracks;
}

} // namespace vehicle::core
