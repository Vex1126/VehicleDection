#include "mjpeg_view.hpp"

#include <QColor>
#include <QIODevice>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPaintEvent>
#include <QRect>
#include <QSize>
#include <QTimer>

MjpegView::MjpegView(QWidget* parent)
    : QWidget(parent),
      manager_(new QNetworkAccessManager(this)),
      reconnectTimer_(new QTimer(this)),
      decodeTimer_(new QTimer(this))
{
    setMinimumSize(420, 210);
    statusText_ = QStringLiteral("Video stream not configured");
    setStyleSheet(QStringLiteral("background:#101418;color:#d0d7de;border:1px solid #30363d;"));

    reconnectTimer_->setInterval(1000);
    reconnectTimer_->setSingleShot(true);
    connect(reconnectTimer_, &QTimer::timeout, this, [this] {
        if (reconnectEnabled_ && reply_ == nullptr) {
            openStream();
        }
    });

    decodeTimer_->setInterval(100);
    connect(decodeTimer_, &QTimer::timeout, this, [this] {
        consumeFrames();
    });
}

MjpegView::~MjpegView()
{
    stop();
}

void MjpegView::start(const QUrl& url)
{
    stop();
    streamUrl_ = url;
    if (!streamUrl_.isValid() || streamUrl_.isEmpty()) {
        setStatusText(QStringLiteral("Video stream not configured"));
        return;
    }

    reconnectEnabled_ = true;
    currentFrame_ = {};
    decodeTimer_->start();
    openStream();
}

void MjpegView::stop()
{
    reconnectEnabled_ = false;
    reconnectTimer_->stop();
    decodeTimer_->stop();
    if (reply_ != nullptr) {
        QNetworkReply* reply = reply_;
        reply_ = nullptr;
        reply->abort();
        reply->deleteLater();
    }
    buffer_.clear();
    currentFrame_ = {};
    update();
}

void MjpegView::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(QStringLiteral("#101418")));

    if (!currentFrame_.isNull() && width() > 0 && height() > 0) {
        QSize scaledSize = currentFrame_.size();
        scaledSize.scale(size(), Qt::KeepAspectRatio);
        const QRect target((width() - scaledSize.width()) / 2,
                           (height() - scaledSize.height()) / 2,
                           scaledSize.width(),
                           scaledSize.height());
        painter.drawImage(target, currentFrame_);
    } else {
        painter.setPen(QColor(QStringLiteral("#d0d7de")));
        painter.drawText(rect().adjusted(12, 12, -12, -12), Qt::AlignCenter | Qt::TextWordWrap, statusText_);
    }

    painter.setPen(QColor(QStringLiteral("#30363d")));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void MjpegView::openStream()
{
    if (reply_ != nullptr || !streamUrl_.isValid() || streamUrl_.isEmpty()) {
        return;
    }

    if (currentFrame_.isNull()) {
        setStatusText(QStringLiteral("Connecting to video stream..."));
    }
    buffer_.clear();

    QNetworkRequest request(streamUrl_);
    request.setRawHeader("Cache-Control", "no-cache");
    QNetworkReply* reply = manager_->get(request);
    reply_ = reply;

    connect(reply, &QIODevice::readyRead, this, [this, reply] {
        if (reply != reply_) {
            return;
        }
        buffer_.append(reply->readAll());
        if (buffer_.size() > 4 * 1024 * 1024) {
            buffer_.remove(0, buffer_.size() - 2 * 1024 * 1024);
        }
    });
    connect(reply,
            QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),
            this,
            [this, reply](QNetworkReply::NetworkError) {
                if (reply == reply_ && currentFrame_.isNull()) {
                    setStatusText(QStringLiteral("Video stream unavailable: %1").arg(reply->errorString()));
                }
            });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != reply_) {
            return;
        }
        reply_ = nullptr;
        reply->deleteLater();
        buffer_.clear();
        if (currentFrame_.isNull() && reply->error() == QNetworkReply::NoError) {
            setStatusText(QStringLiteral("Video stream ended"));
        }
        scheduleReconnect();
    });
}

void MjpegView::scheduleReconnect()
{
    if (reconnectEnabled_ && !reconnectTimer_->isActive()) {
        reconnectTimer_->start();
    }
}

void MjpegView::consumeFrames()
{
    static const QByteArray jpegStart("\xff\xd8", 2);
    static const QByteArray jpegEnd("\xff\xd9", 2);

    const int end = buffer_.lastIndexOf(jpegEnd);
    if (end < 0) {
        if (buffer_.size() > 2 * 1024 * 1024) {
            buffer_.clear();
        }
        return;
    }

    const int start = buffer_.lastIndexOf(jpegStart, end);
    if (start < 0) {
        buffer_.remove(0, end + jpegEnd.size());
        return;
    }

    const int length = end + jpegEnd.size() - start;
    const QImage image = QImage::fromData(buffer_.mid(start, length), "JPEG");
    buffer_.remove(0, end + jpegEnd.size());
    if (!image.isNull()) {
        currentFrame_ = image;
        update();
    }
}

void MjpegView::setStatusText(const QString& text)
{
    statusText_ = text;
    update();
}
