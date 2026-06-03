#include "vehicle/infra/logger.hpp"

#include <chrono>
#include <ctime>
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <sstream>

namespace vehicle::infra {

namespace {

const char* toString(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "INFO";
}

std::string now()
{
    const auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%F %T");
    return out.str();
}

} // namespace

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

void Logger::configure(LogLevel level, const std::string& filePath, bool consoleEnabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
    consoleEnabled_ = consoleEnabled;
    if (file_.is_open()) {
        file_.close();
    }
    if (!filePath.empty()) {
        const auto parent = fs::path(filePath).parent_path();
        if (!parent.empty()) {
            fs::create_directories(parent);
        }
        file_.open(filePath, std::ios::app);
        if (!file_) {
            throw std::runtime_error("failed to open log file: " + filePath);
        }
    }
}

void Logger::setLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

void Logger::log(LogLevel level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level) < static_cast<int>(level_)) {
        return;
    }
    const auto line = now() + " [" + toString(level) + "] " + message;
    if (consoleEnabled_) {
        std::cout << line << '\n';
    }
    if (file_.is_open()) {
        file_ << line << '\n';
        file_.flush();
    }
}

LogLevel parseLogLevel(const std::string& value)
{
    if (value == "debug" || value == "DEBUG") {
        return LogLevel::Debug;
    }
    if (value == "warning" || value == "warn" || value == "WARNING" || value == "WARN") {
        return LogLevel::Warning;
    }
    if (value == "error" || value == "ERROR") {
        return LogLevel::Error;
    }
    return LogLevel::Info;
}

} // namespace vehicle::infra
