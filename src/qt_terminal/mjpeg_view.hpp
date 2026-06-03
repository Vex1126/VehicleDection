#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QUrl>
#include <QWidget>

class QNetworkAccessManager;
class QNetworkReply;
class QPaintEvent;
class QTimer;

class MjpegView final : public QWidget {
public:
    explicit MjpegView(QWidget* parent = nullptr);
    ~MjpegView() override;

    void start(const QUrl& url);
    void stop();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void openStream();
    void scheduleReconnect();
    void consumeFrames();
    void setStatusText(const QString& text);

    QNetworkAccessManager* manager_{nullptr};
    QNetworkReply* reply_{nullptr};
    QTimer* reconnectTimer_{nullptr};
    QTimer* decodeTimer_{nullptr};
    QByteArray buffer_;
    QImage currentFrame_;
    QUrl streamUrl_;
    QString statusText_;
    bool reconnectEnabled_{false};
};
