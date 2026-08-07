#include "notificationadapter.h"

#include <QDBusConnection>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(logNotification, "openpet.notification")

namespace {

constexpr auto kService = "org.freedesktop.Notifications";
constexpr auto kPath = "/org/freedesktop/Notifications";
constexpr auto kInterface = "org.freedesktop.Notifications";

// Причины закрытия из спецификации freedesktop. Нас интересует различие
// между «показали и оно истекло» и «пользователь убрал сам»: во втором
// случае он уведомление уже увидел, и питомцу сообщать не о чем.
constexpr quint32 kReasonExpired = 1;
constexpr quint32 kReasonDismissed = 2;

} // namespace

NotificationAdapter::NotificationAdapter(QObject *parent)
    : EventSource(parent)
{
}

void NotificationAdapter::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        setCapability(CapabilityState::Unsupported,
                      QStringLiteral("сессионная шина D-Bus недоступна"));
        return;
    }

    const bool connected = bus.connect(QString::fromLatin1(kService),
                                       QString::fromLatin1(kPath),
                                       QString::fromLatin1(kInterface),
                                       QStringLiteral("NotificationClosed"),
                                       this,
                                       SLOT(onNotificationClosed(quint32, quint32)));

    if (!connected) {
        // Сервера уведомлений может не быть вовсе — это штатная ситуация:
        // питомец просто не будет реагировать на уведомления (§10).
        setCapability(CapabilityState::Unsupported,
                      QStringLiteral("сервер уведомлений не отвечает"));
        return;
    }

    // Degraded, а не available: наблюдается закрытие, а не появление.
    // Помечать источник полностью исправным было бы неправдой.
    setCapability(CapabilityState::Degraded,
                  QStringLiteral("видно закрытие уведомления, а не появление"));
}

void NotificationAdapter::onNotificationClosed(quint32 id, quint32 reason)
{
    Q_UNUSED(id)

    if (reason != kReasonExpired && reason != kReasonDismissed) {
        // Закрытие по вызову CloseNotification означает, что уведомление
        // убрало само приложение — пользователь мог его и не увидеть.
        return;
    }

    qCDebug(logNotification, "уведомление закрыто, причина %u", reason);
    emit notificationOccurred();
}
