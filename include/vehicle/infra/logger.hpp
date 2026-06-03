#pragma once

#include <mutex>
#include <fstream>
#include <string>

namespace vehicle::infra {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    static Logger& instance();

    void configure(LogLevel level, const std::string& filePath, bool consoleEnabled = true);
    void setLevel(LogLevel level);
    void log(LogLevel level, const std::string& message);

private:
    Logger() = default;

    LogLevel level_{LogLevel::Info};
    bool consoleEnabled_{true};
    std::ofstream file_;
    std::mutex mutex_;
};

[[nodiscard]] LogLevel parseLogLevel(const std::string& value);

} // namespace vehicle::infra
