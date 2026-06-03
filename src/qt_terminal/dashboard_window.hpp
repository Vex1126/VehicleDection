#pragma once

#include "mqtt_bridge.hpp"

#include <QJsonObject>
#include <QMainWindow>

class QLabel;
class MjpegView;
class QUrl;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class SocketCanReader;

class DashboardWindow final : public QMainWindow {
public:
    explicit DashboardWindow(const MqttBridge::Config& mqttConfig,
                             const QString& canInterface,
                             const QUrl& videoUrl,
                             QWidget* parent = nullptr);

private:
    void connectServices();
    void handleMessage(const QString& topic, const QByteArray& payload);
    void updateVehicleState(const QJsonObject& state);
    void addEvent(const QJsonObject& event);
    void setConnectionState(bool connected, const QString& detail);
    static QString numberText(const QJsonObject& object, const QString& name, const QString& suffix = {});

    MqttBridge* mqtt_{nullptr};
    SocketCanReader* can_{nullptr};
    bool localCanActive_{false};
    MjpegView* videoView_{nullptr};

    QLineEdit* hostEdit_{nullptr};
    QSpinBox* portEdit_{nullptr};
    QLineEdit* prefixEdit_{nullptr};
    QLineEdit* canInterfaceEdit_{nullptr};
    QLabel* connectionLabel_{nullptr};
    QLabel* canStatusLabel_{nullptr};
    QLabel* alertLabel_{nullptr};
    QLabel* frameLabel_{nullptr};
    QLabel* detectionsLabel_{nullptr};
    QLabel* tracksLabel_{nullptr};
    QLabel* fpsLabel_{nullptr};
    QLabel* cpuLabel_{nullptr};
    QLabel* speedLabel_{nullptr};
    QLabel* steeringLabel_{nullptr};
    QLabel* brakeLabel_{nullptr};
    QLabel* throttleLabel_{nullptr};
    QLabel* gearLabel_{nullptr};
    QLabel* signalLabel_{nullptr};
    QTableWidget* eventsTable_{nullptr};
};
