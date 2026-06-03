#pragma once

#include <array>
#include <chrono>
#include <deque>
#include <string>
#include <vector>

#if defined(VEHICLE_WITH_OPENCV)
#include <opencv2/core.hpp>
#endif

namespace vehicle::core {

enum class ObjectClass {
    Car,
    Truck,
    Bus,
    Motorcycle,
    Bicycle,
    Pedestrian,
    TrafficLight,
    Unknown
};

enum class RiskLevel {
    Low,
    Medium,
    High,
    Critical
};

struct Point {
    double x{0.0};
    double y{0.0};
};

struct Rect {
    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    [[nodiscard]] double area() const;
    [[nodiscard]] Point center() const;
};

struct Detection {
    Rect box;
    float confidence{0.0F};
    ObjectClass label{ObjectClass::Unknown};
    std::string className;
};

struct Frame {
    int index{0};
    int width{1280};
    int height{720};
    double fps{0.0};
    std::chrono::steady_clock::time_point timestamp{std::chrono::steady_clock::now()};
    std::vector<Detection> syntheticDetections;
#if defined(VEHICLE_WITH_OPENCV)
    cv::Mat image;
#endif
};

struct Track {
    int id{0};
    Detection detection;
    Point velocity;
    Point kalmanPosition;
    Point kalmanVelocity; 
    std::array<double, 16> kalmanCovariance{};
    bool kalmanInitialized{false};
    int age{0};
    int missedFrames{0};
    std::deque<Point> trajectory;
};

struct BehaviorEvent {
    int trackId{0};
    std::string behavior;
    RiskLevel risk{RiskLevel::Low};
    std::string evidence;
    Rect targetBox;
    std::string objectClass;
};

[[nodiscard]] const char* toString(ObjectClass label);
[[nodiscard]] const char* toString(RiskLevel level);
[[nodiscard]] double intersectionOverUnion(const Rect& lhs, const Rect& rhs);

} // namespace vehicle::core
