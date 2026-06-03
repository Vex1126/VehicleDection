#pragma once

#include <cstddef>
#include <chrono>

namespace vehicle::infra {

struct PerformanceSnapshot {
    double fps{0.0};
    double averageFrameMs{0.0};
    double cpuPercent{0.0};
    std::size_t memoryBytes{0};
    std::size_t processedFrames{0};
};

class PerformanceMonitor {
public:
    PerformanceMonitor();

    void frameProcessed(double elapsedMs);
    [[nodiscard]] PerformanceSnapshot snapshot() const;

private:
    std::size_t frameCount_{0};
    double totalFrameMs_{0.0};
    std::chrono::steady_clock::time_point start_{std::chrono::steady_clock::now()};
    double startProcessCpuSeconds_{0.0};
};

} // namespace vehicle::infra
