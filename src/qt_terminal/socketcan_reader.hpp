#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

class QSocketNotifier;

class SocketCanReader final : public QObject {
    Q_OBJECT

public:
    explicit SocketCanReader(QObject* parent = nullptr);
    ~SocketCanReader() override;

    bool start(const QString& interfaceName, QString* errorMessage = nullptr);
    void stop();

signals:
    void stateUpdated(const QJsonObject& state);
    void errorOccurred(const QString& message);

private:
    void readAvailableFrames();
    void applyFrame(unsigned int canId, const unsigned char* data, int size);

    int socketFd_{-1};
    QSocketNotifier* notifier_{nullptr};
    QString interfaceName_;
    QJsonObject state_;
};
