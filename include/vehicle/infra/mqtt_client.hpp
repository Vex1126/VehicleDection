#pragma once

#include <memory>
#include <string>

namespace vehicle::infra {

struct MqttConfig {
    bool enabled{false};
    std::string broker{"tcp://127.0.0.1:1883"};
    std::string clientId{"vehicle-edge-001"};
    std::string username;
    std::string password;
    int qos{1};
    bool retain{false};
    int keepAliveSeconds{30};
};

class MqttClient {
public:
    explicit MqttClient(MqttConfig config);
    ~MqttClient();

    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;

    [[nodiscard]] bool connect();
    void disconnect();
    [[nodiscard]] bool publish(const std::string& topic, const std::string& payload);
    [[nodiscard]] bool enabled() const;
    [[nodiscard]] bool connected() const;

private:
    struct Impl;

    MqttConfig config_;
    std::unique_ptr<Impl> impl_;
};

} // namespace vehicle::infra
