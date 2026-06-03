#include "vehicle/infra/performance_monitor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>
#include <sstream>
#include <string>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace vehicle::infra {

namespace {

std::size_t currentRssBytes()
{
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream input(line);
            std::string key;
            std::size_t valueKb = 0;
            std::string unit;
            input >> key >> valueKb >> unit;
            return valueKb * 1024;
        }
    }
#endif
    return 0;
}

double currentProcessCpuSeconds()
{
#if defined(__linux__)
    std::ifstream stat("/proc/self/stat");
    std::string line;
    std::getline(stat, line);
    const auto closeParen = line.rfind(')');
    if (closeParen == std::string::npos || closeParen + 2 >= line.size()) {
        return 0.0;
    }

    std::istringstream input(line.substr(closeParen + 2));
    std::string token;
    long long userTicks = 0;
    long long kernelTicks = 0;
    for (int field = 3; input >> token; ++field) {
        if (field == 14) {
            userTicks = std::stoll(token);
        } else if (field == 15) {
            kernelTicks = std::stoll(token);
            break;
        }
    }

    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    if (ticksPerSecond <= 0) {
        return 0.0;
    }
    return static_cast<double>(userTicks + kernelTicks) / static_cast<double>(ticksPerSecond);
#else
    return 0.0;
#endif
}

} // namespace

PerformanceMonitor::PerformanceMonitor() : startProcessCpuSeconds_(currentProcessCpuSeconds()) {}

void PerformanceMonitor::frameProcessed(double elapsedMs)
{
    frameCount_ += 1;
    totalFrameMs_ += elapsedMs;
}

PerformanceSnapshot PerformanceMonitor::snapshot() const
{
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    const auto cpuElapsed = std::max(0.0, currentProcessCpuSeconds() - startProcessCpuSeconds_);
    const auto cores = std::max(1U, std::thread::hardware_concurrency());
    PerformanceSnapshot snapshot;
    snapshot.fps = elapsed > 0.0 ? static_cast<double>(frameCount_) / elapsed : 0.0;
    snapshot.averageFrameMs = frameCount_ > 0 ? totalFrameMs_ / static_cast<double>(frameCount_) : 0.0;
    snapshot.cpuPercent = elapsed > 0.0 ? std::min(100.0, cpuElapsed / elapsed * 100.0 / cores) : 0.0;
    snapshot.memoryBytes = currentRssBytes();
    snapshot.processedFrames = frameCount_;
    return snapshot;
}

} // namespace vehicle::infra
