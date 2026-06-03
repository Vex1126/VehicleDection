#include "mqtt_bridge.hpp"

#include <mosquitto.h>

#include <QByteArray>

#include <utility>

MqttBridge::MqttBridge(Config config, QObject* parent)
    : QObject(parent),
      config_(std::move(config))
{
    while (config_.topicPrefix.endsWith('/')) {
        config_.topicPrefix.chop(1);
    }
}

MqttBridge::~MqttBridge()
{
    stop();
}

bool MqttBridge::start(QString* errorMessage)
{
    if (client_ != nullptr) {
        return true;
    }
    if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Cannot initialize libmosquitto");
        }
        return false;
    }
    libraryInitialized_ = true;

    const QByteArray clientId = config_.clientId.toUtf8();
    client_ = mosquitto_new(clientId.constData(), true, this);
    if (client_ == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Cannot create MQTT client");
        }
        stop();
        return false;
    }

    mosquitto_connect_callback_set(client_, &MqttBridge::onConnect);
    mosquitto_disconnect_callback_set(client_, &MqttBridge::onDisconnect);
    mosquitto_message_callback_set(client_, &MqttBridge::onMessage);
    mosquitto_reconnect_delay_set(client_, 1, 30, true);

    if (!config_.username.isEmpty()) {
        const QByteArray username = config_.username.toUtf8();
        const QByteArray password = config_.password.toUtf8();
        mosquitto_username_pw_set(client_, username.constData(), password.constData());
    }

    const QByteArray host = config_.host.toUtf8();
    const int connectCode = mosquitto_connect_async(client_, host.constData(), config_.port, config_.keepAliveSeconds);
    if (connectCode != MOSQ_ERR_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = QString::fromUtf8(mosquitto_strerror(connectCode));
        }
        stop();
        return false;
    }
    const int loopCode = mosquitto_loop_start(client_);
    if (loopCode != MOSQ_ERR_SUCCESS) {
        if (errorMessage != nullptr) {
            *errorMessage = QString::fromUtf8(mosquitto_strerror(loopCode));
        }
        stop();
        return false;
    }

    emit connectionChanged(false, QStringLiteral("Connecting to MQTT broker..."));
    return true;
}

void MqttBridge::stop()
{
    connected_ = false;
    if (client_ != nullptr) {
        mosquitto_disconnect(client_);
        mosquitto_loop_stop(client_, true);
        mosquitto_destroy(client_);
        client_ = nullptr;
    }
    if (libraryInitialized_) {
        mosquitto_lib_cleanup();
        libraryInitialized_ = false;
    }
}

bool MqttBridge::publish(const QString& relativeTopic, const QByteArray& payload)
{
    if (!connected_ || client_ == nullptr) {
        return false;
    }
    const QByteArray topic = fullTopic(relativeTopic).toUtf8();
    return mosquitto_publish(client_, nullptr, topic.constData(), payload.size(), payload.constData(), 1, false) ==
           MOSQ_ERR_SUCCESS;
}

void MqttBridge::onConnect(mosquitto* client, void* context, int code)
{
    auto* bridge = static_cast<MqttBridge*>(context);
    if (code != MOSQ_ERR_SUCCESS) {
        bridge->connected_ = false;
        emit bridge->connectionChanged(false, QString::fromUtf8(mosquitto_connack_string(code)));
        return;
    }
    const QByteArray subscription = bridge->fullTopic(QStringLiteral("#")).toUtf8();
    const int subscribeCode = mosquitto_subscribe(client, nullptr, subscription.constData(), 1);
    bridge->connected_ = subscribeCode == MOSQ_ERR_SUCCESS;
    const QString detail = bridge->connected_
                               ? QStringLiteral("Connected: %1").arg(QString::fromUtf8(subscription))
                               : QString::fromUtf8(mosquitto_strerror(subscribeCode));
    emit bridge->connectionChanged(bridge->connected_, detail);
}

void MqttBridge::onDisconnect(mosquitto*, void* context, int code)
{
    auto* bridge = static_cast<MqttBridge*>(context);
    bridge->connected_ = false;
    emit bridge->connectionChanged(false,
                                   code == MOSQ_ERR_SUCCESS ? QStringLiteral("MQTT disconnected")
                                                            : QStringLiteral("MQTT connection lost; reconnecting"));
}

void MqttBridge::onMessage(mosquitto*, void* context, const mosquitto_message* message)
{
    auto* bridge = static_cast<MqttBridge*>(context);
    const QString topic = QString::fromUtf8(message->topic);
    const QByteArray payload(static_cast<const char*>(message->payload), message->payloadlen);
    emit bridge->messageReceived(topic, payload);
}

QString MqttBridge::fullTopic(const QString& relativeTopic) const
{
    return config_.topicPrefix + QStringLiteral("/") + relativeTopic;
}
