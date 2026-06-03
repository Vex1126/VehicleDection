#include "dashboard_window.hpp"
#include "mqtt_bridge.hpp"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QUrl>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("vehicle_terminal"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Vehicle embedded Qt MQTT/CAN terminal"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("broker"), QStringLiteral("MQTT broker host"), QStringLiteral("host"),
                      QStringLiteral("127.0.0.1")});
    parser.addOption({QStringLiteral("port"), QStringLiteral("MQTT broker port"), QStringLiteral("port"),
                      QStringLiteral("1883")});
    parser.addOption({QStringLiteral("topic-prefix"), QStringLiteral("Vehicle MQTT topic prefix"),
                      QStringLiteral("prefix"), QStringLiteral("vehicle/vehicle-001")});
    parser.addOption({QStringLiteral("can-interface"), QStringLiteral("Local SocketCAN interface"),
                      QStringLiteral("interface"), QStringLiteral("can0")});
    parser.addOption({QStringLiteral("video-url"), QStringLiteral("Annotated MJPEG stream URL"),
                      QStringLiteral("url")});
    parser.addOption({QStringLiteral("fullscreen"), QStringLiteral("Start as a fullscreen embedded UI")});
    parser.process(application);

    MqttBridge::Config config;
    config.host = parser.value(QStringLiteral("broker"));
    config.port = parser.value(QStringLiteral("port")).toInt();
    config.topicPrefix = parser.value(QStringLiteral("topic-prefix"));
    DashboardWindow window(config, parser.value(QStringLiteral("can-interface")),
                           QUrl(parser.value(QStringLiteral("video-url"))));
    if (parser.isSet(QStringLiteral("fullscreen"))) {
        window.showFullScreen();
    } else {
        window.show();
    }
    return application.exec();
}
