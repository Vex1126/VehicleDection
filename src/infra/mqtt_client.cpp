#include "vehicle/infra/mqtt_client.hpp"

#include "vehicle/infra/logger.hpp"

#include <cstdlib>
#include <string>
#include <utility>

#if defined(VEHICLE_WITH_MOSQUITTO)
#include <mosquitto.h>
#endif

namespace vehicle::infra {

namespace {

#if defined(VEHICLE_WITH_MOSQUITTO)
struct MqttEndpoint {
    std::string host{"127.0.0.1"};
    int port{1883};
};

MqttEndpoint parseEndpoint(std::string broker)
{
    constexpr const char* tcpPrefix = "tcp://";
    constexpr const char* mqttPrefix = "mqtt://";
    if (broker.rfind(tcpPrefix, 0) == 0) {
        broker.erase(0, std::string(tcpPrefix).size());
    } else if (broker.rfind(mqttPrefix, 0) == 0) {
        broker.erase(0, std::string(mqttPrefix).size());
    }

    MqttEndpoint endpoint;
    const auto colon = broker.rfind(':');
    if (colon != std::string::npos) {
        endpoint.host = broker.substr(0, colon);
        endpoint.port = std::atoi(broker.substr(colon + 1).c_str());
        if (endpoint.port <= 0) {
            endpoint.port = 1883;
        }
    } else if (!broker.empty()) {
        endpoint.host = broker;
    }
    return endpoint;
}
#endif

} // namespace

struct MqttClient::Impl {
#if defined(VEHICLE_WITH_MOSQUITTO)
    mosquitto* client{nullptr};
#endif
    bool connected{false};
};

MqttClient::MqttClient(MqttConfig config) : config_(std::move(config)), impl_(std::make_unique<Impl>()) {}

MqttClient::~MqttClient()
{
    disconnect();
}

bool MqttClient::connect()
{
    if (!config_.enabled) {
        return true;
    }
    if (impl_->connected) {
        return true;
    }

#if defined(VEHICLE_WITH_MOSQUITTO)
    mosquitto_lib_init();
    impl_->client = mosquitto_new(config_.clientId.c_str(), true, nullptr);
    if (impl_->client == nullptr) {
        Logger::instance().log(LogLevel::Warning, "MQTT client creation failed");
        return false;
    }

    if (!config_.username.empty()) {
        mosquitto_username_pw_set(impl_->client, config_.username.c_str(), config_.password.c_str());
    }

    const auto endpoint = parseEndpoint(config_.broker);
    const int code = mosquitto_connect(impl_->client,
                                       endpoint.host.c_str(),
                                       endpoint.port,
                                       config_.keepAliveSeconds);
    if (code != MOSQ_ERR_SUCCESS) {
        Logger::instance().log(LogLevel::Warning,
                               "MQTT connect failed for " + config_.broker + ": " + mosquitto_strerror(code));
        mosquitto_destroy(impl_->client);
        impl_->client = nullptr;
        return false;
    }

    const int loopCode = mosquitto_loop_start(impl_->client);
    if (loopCode != MOSQ_ERR_SUCCESS) {
        Logger::instance().log(LogLevel::Warning, "MQTT loop start failed: " + std::string(mosquitto_strerror(loopCode)));
        mosquitto_disconnect(impl_->client);
        mosquitto_destroy(impl_->client);
        impl_->client = nullptr;
        return false;
    }

    impl_->connected = true;
    Logger::instance().log(LogLevel::Info, "MQTT connected to " + config_.broker);
    return true;
#else
    Logger::instance().log(LogLevel::Warning, "MQTT disabled because libmosquitto was not found at build time");
    return false;
#endif
}

void MqttClient::disconnect()
{
    if (!config_.enabled) {
        return;
    }
#if defined(VEHICLE_WITH_MOSQUITTO)
    if (impl_->client != nullptr) {
        mosquitto_loop_stop(impl_->client, true);
        mosquitto_disconnect(impl_->client);
        mosquitto_destroy(impl_->client);
        impl_->client = nullptr;
        mosquitto_lib_cleanup();
    }
#endif
    impl_->connected = false;
}

bool MqttClient::publish(const std::string& topic, const std::string& payload)
{
    if (!config_.enabled) {
        return true;
    }
    if (!impl_->connected && !connect()) {
        return false;
    }

#if defined(VEHICLE_WITH_MOSQUITTO)
    const int code = mosquitto_publish(impl_->client,
                                       nullptr,
                                       topic.c_str(),
                                       static_cast<int>(payload.size()),
                                       payload.data(),
                                       config_.qos,
                                       config_.retain);
    if (code != MOSQ_ERR_SUCCESS) {
        Logger::instance().log(LogLevel::Warning, "MQTT publish failed: " + std::string(mosquitto_strerror(code)));
        impl_->connected = false;
        return false;
    }
    return true;
#else
    (void)topic;
    (void)payload;
    return false;
#endif
}

bool MqttClient::enabled() const
{
    return config_.enabled;
}

bool MqttClient::connected() const
{
    return impl_->connected;
}

} // namespace vehicle::infra
