#include "dashboard_window.hpp"

#include "socketcan_reader.hpp"
#include "mjpeg_view.hpp"

#include <QByteArray>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QDebug>
#include <QSizePolicy>
#include <QVBoxLayout>

DashboardWindow::DashboardWindow(const MqttBridge::Config& mqttConfig,
                                 const QString& canInterface,
                                 const QUrl& videoUrl,
                                 QWidget* parent)
    : QMainWindow(parent),
      can_(new SocketCanReader(this))
{
    setWindowTitle(QStringLiteral("Vehicle Embedded Terminal"));
    resize(1024, 600);
    setMinimumSize(800, 480);
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* videoBox = new QWidget(central);
    videoBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* videoLayout = new QVBoxLayout(videoBox);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoView_ = new MjpegView(videoBox);
    videoView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    qInfo() << "vehicle_terminal_layout adaptive video area, side=220";
    videoLayout->addWidget(videoView_);
    root->addWidget(videoBox, 1);

    auto* sidePanel = new QWidget(central);
    sidePanel->setFixedWidth(220);
    sidePanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto* sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(6, 4, 6, 4);
    sideLayout->setSpacing(5);

    hostEdit_ = new QLineEdit(mqttConfig.host, central);
    portEdit_ = new QSpinBox(central);
    portEdit_->setRange(1, 65535);
    portEdit_->setValue(mqttConfig.port);
    prefixEdit_ = new QLineEdit(mqttConfig.topicPrefix, central);
    canInterfaceEdit_ = new QLineEdit(canInterface, central);
    connectionLabel_ = new QLabel(QStringLiteral("MQTT: disconnected"), central);
    canStatusLabel_ = new QLabel(QStringLiteral("CAN: stopped"), central);
    hostEdit_->hide();
    portEdit_->hide();
    prefixEdit_->hide();
    canInterfaceEdit_->hide();
    connectionLabel_->hide();
    canStatusLabel_->hide();

    auto* inferenceBox = new QGroupBox(QStringLiteral("Remote Inference"), sidePanel);
    auto* inferenceLayout = new QFormLayout(inferenceBox);
    inferenceLayout->setContentsMargins(6, 6, 6, 6);
    frameLabel_ = new QLabel(QStringLiteral("--"));
    detectionsLabel_ = new QLabel(QStringLiteral("--"));
    tracksLabel_ = new QLabel(QStringLiteral("--"));
    fpsLabel_ = new QLabel(QStringLiteral("--"));
    cpuLabel_ = new QLabel(QStringLiteral("--"));
    inferenceLayout->addRow(QStringLiteral("Frame"), frameLabel_);
    inferenceLayout->addRow(QStringLiteral("Detections"), detectionsLabel_);
    inferenceLayout->addRow(QStringLiteral("Tracks"), tracksLabel_);
    inferenceLayout->addRow(QStringLiteral("FPS"), fpsLabel_);
    inferenceLayout->addRow(QStringLiteral("CPU"), cpuLabel_);
    sideLayout->addWidget(inferenceBox);

    auto* vehicleBox = new QGroupBox(QStringLiteral("Local Vehicle CAN"), sidePanel);
    auto* vehicleLayout = new QFormLayout(vehicleBox);
    vehicleLayout->setContentsMargins(6, 6, 6, 6);
    speedLabel_ = new QLabel(QStringLiteral("--"));
    steeringLabel_ = new QLabel(QStringLiteral("--"));
    brakeLabel_ = new QLabel(QStringLiteral("--"));
    throttleLabel_ = new QLabel(QStringLiteral("--"));
    gearLabel_ = new QLabel(QStringLiteral("--"));
    signalLabel_ = new QLabel(QStringLiteral("--"));
    vehicleLayout->addRow(QStringLiteral("Speed"), speedLabel_);
    vehicleLayout->addRow(QStringLiteral("Steering"), steeringLabel_);
    vehicleLayout->addRow(QStringLiteral("Brake"), brakeLabel_);
    vehicleLayout->addRow(QStringLiteral("Throttle"), throttleLabel_);
    vehicleLayout->addRow(QStringLiteral("Gear"), gearLabel_);
    vehicleLayout->addRow(QStringLiteral("Signal"), signalLabel_);
    sideLayout->addWidget(vehicleBox);

    auto* alertBox = new QGroupBox(QStringLiteral("Risk Events"), sidePanel);
    auto* alertLayout = new QVBoxLayout(alertBox);
    alertLayout->setContentsMargins(6, 6, 6, 6);
    alertLabel_ = new QLabel(QStringLiteral("No active risk event"), alertBox);
    alertLabel_->setWordWrap(true);
    eventsTable_ = new QTableWidget(0, 2, alertBox);
    eventsTable_->setHorizontalHeaderLabels({QStringLiteral("Risk"), QStringLiteral("Behavior")});
    eventsTable_->horizontalHeader()->setStretchLastSection(true);
    eventsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    eventsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    eventsTable_->verticalHeader()->setVisible(false);
    eventsTable_->setWordWrap(true);
    alertLayout->addWidget(alertLabel_);
    alertLayout->addWidget(eventsTable_);
    sideLayout->addWidget(alertBox, 2);

    root->addWidget(sidePanel);

    setCentralWidget(central);
    setStyleSheet(QStringLiteral("QGroupBox{font-weight:bold;margin-top:6px;} "
                                 "QGroupBox::title{subcontrol-origin:margin;left:6px;} "
                                 "QLabel{font-size:12px;} QLineEdit,QSpinBox{font-size:12px;} "
                                 "QTableWidget{font-size:11px;}"));

    connect(can_, &SocketCanReader::stateUpdated, this, [this](const QJsonObject& state) {
        updateVehicleState(state);
        if (mqtt_ != nullptr) {
            mqtt_->publish(QStringLiteral("terminal/can/state"), QJsonDocument(state).toJson(QJsonDocument::Compact));
        }
    });
    connect(can_, &SocketCanReader::errorOccurred, this, [this](const QString& message) {
        canStatusLabel_->setText(QStringLiteral("CAN error: %1").arg(message));
    });
    connectServices();
    if (!videoUrl.isEmpty()) {
        videoView_->start(videoUrl);
    }
}

void DashboardWindow::connectServices()
{
    if (mqtt_ != nullptr) {
        mqtt_->stop();
        delete mqtt_;
        mqtt_ = nullptr;
    }
    MqttBridge::Config config;
    config.host = hostEdit_->text().trimmed();
    config.port = portEdit_->value();
    config.clientId = QStringLiteral("vehicle-terminal-001");
    config.topicPrefix = prefixEdit_->text().trimmed();
    mqtt_ = new MqttBridge(config, this);
    connect(mqtt_, &MqttBridge::connectionChanged, this, &DashboardWindow::setConnectionState, Qt::QueuedConnection);
    connect(mqtt_, &MqttBridge::messageReceived, this, &DashboardWindow::handleMessage, Qt::QueuedConnection);
    QString error;
    if (!mqtt_->start(&error)) {
        setConnectionState(false, error);
    }

    localCanActive_ = can_->start(canInterfaceEdit_->text().trimmed(), &error);
    canStatusLabel_->setText(localCanActive_ ? QStringLiteral("CAN: listening on %1").arg(canInterfaceEdit_->text())
                                            : QStringLiteral("CAN unavailable: %1").arg(error));
}

void DashboardWindow::handleMessage(const QString& topic, const QByteArray& payload)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const QJsonObject object = document.object();
    if (topic.endsWith(QStringLiteral("/frames"))) {
        frameLabel_->setText(numberText(object, QStringLiteral("frame_index")));
        detectionsLabel_->setText(numberText(object, QStringLiteral("detections")));
        tracksLabel_->setText(numberText(object, QStringLiteral("tracks")));
        fpsLabel_->setText(numberText(object, QStringLiteral("fps")));
        if (!localCanActive_ && object.value(QStringLiteral("vehicle_state")).isObject()) {
            updateVehicleState(object.value(QStringLiteral("vehicle_state")).toObject());
        }
    } else if (topic.endsWith(QStringLiteral("/events"))) {
        addEvent(object);
    } else if (topic.endsWith(QStringLiteral("/status"))) {
        fpsLabel_->setText(numberText(object, QStringLiteral("fps")));
        cpuLabel_->setText(numberText(object, QStringLiteral("cpu_percent"), QStringLiteral(" %")));
    } else if (topic.endsWith(QStringLiteral("/can/state")) && !localCanActive_) {
        updateVehicleState(object);
    }
}

void DashboardWindow::updateVehicleState(const QJsonObject& state)
{
    speedLabel_->setText(numberText(state, QStringLiteral("speed_kph"), QStringLiteral(" km/h")));
    steeringLabel_->setText(numberText(state, QStringLiteral("steering_angle_deg"), QStringLiteral(" deg")));
    throttleLabel_->setText(numberText(state, QStringLiteral("throttle_percent"), QStringLiteral(" %")));
    gearLabel_->setText(numberText(state, QStringLiteral("gear")));
    signalLabel_->setText(state.value(QStringLiteral("turn_signal")).toString(QStringLiteral("--")));
    brakeLabel_->setText(state.contains(QStringLiteral("brake_pressed"))
                             ? (state.value(QStringLiteral("brake_pressed")).toBool() ? QStringLiteral("ON")
                                                                                       : QStringLiteral("OFF"))
                             : QStringLiteral("--"));
}

void DashboardWindow::addEvent(const QJsonObject& event)
{
    const QString risk = event.value(QStringLiteral("risk")).toString(QStringLiteral("low"));
    const QString objectClass = event.value(QStringLiteral("object_class")).toString();
    const QString behavior = event.value(QStringLiteral("behavior")).toString();
    const int trackId = event.value(QStringLiteral("track_id")).toInt();
    qInfo().noquote() << QStringLiteral("risk_event risk=%1 object=%2 behavior=%3 track=%4 evidence=%5")
                             .arg(risk, objectClass, behavior, QString::number(trackId),
                                  event.value(QStringLiteral("evidence")).toString());
    alertLabel_->setText(QStringLiteral("%1: %2").arg(risk.toUpper(), behavior));
    const QString color = risk == QStringLiteral("critical") ? QStringLiteral("#bf1d28")
                          : risk == QStringLiteral("high")   ? QStringLiteral("#df6b00")
                                                               : QStringLiteral("#a37a00");
    alertLabel_->setStyleSheet(QStringLiteral("font-size:15px;font-weight:bold;color:%1;").arg(color));
    eventsTable_->insertRow(0);
    eventsTable_->setItem(0, 0, new QTableWidgetItem(risk));
    eventsTable_->setItem(0, 1, new QTableWidgetItem(behavior));
    while (eventsTable_->rowCount() > 30) {
        eventsTable_->removeRow(eventsTable_->rowCount() - 1);
    }
}

void DashboardWindow::setConnectionState(bool connected, const QString& detail)
{
    connectionLabel_->setText(QStringLiteral("MQTT: %1").arg(detail));
    connectionLabel_->setStyleSheet(connected ? QStringLiteral("color:#22863a;") : QStringLiteral("color:#bf1d28;"));
}

QString DashboardWindow::numberText(const QJsonObject& object, const QString& name, const QString& suffix)
{
    if (!object.contains(name) || !object.value(name).isDouble()) {
        return QStringLiteral("--");
    }
    return QString::number(object.value(name).toDouble(), 'f', 1) + suffix;
}
