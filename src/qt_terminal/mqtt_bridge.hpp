#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>

struct mosquitto;
struct mosquitto_message;

class MqttBridge final : public QObject {
    Q_OBJECT

public:
    struct Config {
        QString host{"127.0.0.1"};
        int port{1883};
        QString clientId{"vehicle-terminal-001"};
        QString topicPrefix{"vehicle/vehicle-001"};
        QString username;
        QString password;
        int keepAliveSeconds{30};
    };

    explicit MqttBridge(Config config, QObject* parent = nullptr);
    ~MqttBridge() override;

    bool start(QString* errorMessage = nullptr);
    void stop();
    bool publish(const QString& relativeTopic, const QByteArray& payload);

signals:
    void connectionChanged(bool connected, const QString& detail);
    void messageReceived(const QString& topic, const QByteArray& payload);

private:
    static void onConnect(mosquitto* client, void* context, int code);
    static void onDisconnect(mosquitto* client, void* context, int code);
    static void onMessage(mosquitto* client, void* context, const mosquitto_message* message);

    QString fullTopic(const QString& relativeTopic) const;

    Config config_;
    mosquitto* client_{nullptr};
    bool libraryInitialized_{false};
    std::atomic_bool connected_{false};
};
