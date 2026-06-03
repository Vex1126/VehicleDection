#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vehicle::infra {

class MjpegStreamer {
public:
    MjpegStreamer() = default;
    ~MjpegStreamer();

    MjpegStreamer(const MjpegStreamer&) = delete;
    MjpegStreamer& operator=(const MjpegStreamer&) = delete;

    [[nodiscard]] bool start(int port, std::string* errorMessage = nullptr);
    void stop();
    void publish(std::vector<unsigned char> jpegFrame);

private:
    void serve();

    int listenFd_{-1};
    bool stopping_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable frameReady_;
    std::vector<unsigned char> latestFrame_;
    std::uint64_t latestSequence_{0};
};

} // namespace vehicle::infra
