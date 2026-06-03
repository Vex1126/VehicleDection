#include "socketcan_reader.hpp"

#include <QByteArray>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>

#if defined(Q_OS_LINUX)
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

SocketCanReader::SocketCanReader(QObject* parent) : QObject(parent) {}

SocketCanReader::~SocketCanReader()
{
    stop();
}

bool SocketCanReader::start(const QString& interfaceName, QString* errorMessage)
{
    stop();
    interfaceName_ = interfaceName;

#if defined(Q_OS_LINUX)
    socketFd_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socketFd_ < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QString::fromLocal8Bit(std::strerror(errno));
        }
        return false;
    }

    const QByteArray interfaceBytes = interfaceName.toLocal8Bit();
    const unsigned int index = if_nametoindex(interfaceBytes.constData());
    if (index == 0U) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("CAN interface %1 does not exist").arg(interfaceName);
        }
        stop();
        return false;
    }

    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = static_cast<int>(index);
    if (::bind(socketFd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QString::fromLocal8Bit(std::strerror(errno));
        }
        stop();
        return false;
    }

    const int flags = ::fcntl(socketFd_, F_GETFL, 0);
    ::fcntl(socketFd_, F_SETFL, flags | O_NONBLOCK);
    notifier_ = new QSocketNotifier(socketFd_, QSocketNotifier::Read, this);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(notifier_, &QSocketNotifier::activated, this, [this](QSocketDescriptor, QSocketNotifier::Type) {
        readAvailableFrames();
    });
#else
    connect(notifier_, &QSocketNotifier::activated, this, [this](int) { readAvailableFrames(); });
#endif
    state_ = QJsonObject{{QStringLiteral("valid"), false}, {QStringLiteral("interface"), interfaceName_}};
    return true;
#else
    if (errorMessage != nullptr) {
        *errorMessage = QStringLiteral("SocketCAN is only supported on Linux");
    }
    return false;
#endif
}

void SocketCanReader::stop()
{
    delete notifier_;
    notifier_ = nullptr;
#if defined(Q_OS_LINUX)
    if (socketFd_ >= 0) {
        ::close(socketFd_);
    }
#endif
    socketFd_ = -1;
}

void SocketCanReader::readAvailableFrames()
{
#if defined(Q_OS_LINUX)
    while (socketFd_ >= 0) {
        can_frame frame{};
        const ssize_t bytes = ::read(socketFd_, &frame, sizeof(frame));
        if (bytes == static_cast<ssize_t>(sizeof(frame))) {
            applyFrame(frame.can_id & CAN_EFF_MASK, frame.data, frame.can_dlc);
            continue;
        }
        if (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            emit errorOccurred(QString::fromLocal8Bit(std::strerror(errno)));
        }
        break;
    }
#endif
}

void SocketCanReader::applyFrame(unsigned int canId, const unsigned char* data, int size)
{
    if (size < 2) {
        return;
    }
    const unsigned int value = (static_cast<unsigned int>(data[0]) << 8U) | data[1];
    switch (canId) {
    case 0x100:
        state_[QStringLiteral("speed_kph")] = static_cast<double>(value) * 0.01;
        break;
    case 0x101:
        state_[QStringLiteral("steering_angle_deg")] = static_cast<double>(static_cast<qint16>(value)) * 0.1;
        break;
    case 0x102:
        state_[QStringLiteral("brake_pressed")] = data[0] != 0;
        state_[QStringLiteral("throttle_percent")] = static_cast<double>(data[1]);
        break;
    case 0x103:
        state_[QStringLiteral("gear")] = static_cast<int>(static_cast<qint8>(data[0]));
        switch (data[1]) {
        case 1:
            state_[QStringLiteral("turn_signal")] = QStringLiteral("left");
            break;
        case 2:
            state_[QStringLiteral("turn_signal")] = QStringLiteral("right");
            break;
        case 3:
            state_[QStringLiteral("turn_signal")] = QStringLiteral("hazard");
            break;
        default:
            state_[QStringLiteral("turn_signal")] = QStringLiteral("none");
            break;
        }
        break;
    default:
        return;
    }
    state_[QStringLiteral("valid")] = true;
    state_[QStringLiteral("interface")] = interfaceName_;
    emit stateUpdated(state_);
}
