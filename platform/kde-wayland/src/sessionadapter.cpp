#include "sessionadapter.h"

#include <QDBusConnection>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(logSession, "openpet.session")

namespace {

constexpr auto kLogin1Service = "org.freedesktop.login1";
constexpr auto kLogin1Path = "/org/freedesktop/login1";
constexpr auto kLogin1Manager = "org.freedesktop.login1.Manager";

// Хранитель экрана Plasma отвечает на общий интерфейс freedesktop,
// поэтому специфичного для KDE имени здесь нет.
constexpr auto kScreensaverService = "org.freedesktop.ScreenSaver";
constexpr auto kScreensaverPath = "/org/freedesktop/ScreenSaver";
constexpr auto kScreensaverInterface = "org.freedesktop.ScreenSaver";

} // namespace

SessionAdapter::SessionAdapter(QObject *parent)
    : EventSource(parent)
{
}

bool SessionAdapter::connectSleep()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected())
        return false;

    return bus.connect(QString::fromLatin1(kLogin1Service),
                       QString::fromLatin1(kLogin1Path),
                       QString::fromLatin1(kLogin1Manager),
                       QStringLiteral("PrepareForSleep"),
                       this,
                       SLOT(onPrepareForSleep(bool)));
}

bool SessionAdapter::connectLock()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    return bus.connect(QString::fromLatin1(kScreensaverService),
                       QString::fromLatin1(kScreensaverPath),
                       QString::fromLatin1(kScreensaverInterface),
                       QStringLiteral("ActiveChanged"),
                       this,
                       SLOT(onScreensaverActiveChanged(bool)));
}

void SessionAdapter::start()
{
    const bool sleep = connectSleep();
    const bool lock = connectLock();

    if (sleep && lock) {
        setCapability(CapabilityState::Available);
    } else if (sleep || lock) {
        // Половина источника лучше, чем ничего: питомец будет реагировать
        // на то, что доступно, и промолчит об остальном (§10).
        setCapability(CapabilityState::Degraded,
                      sleep ? QStringLiteral("блокировка экрана не наблюдается")
                            : QStringLiteral("сон и пробуждение не наблюдаются"));
    } else {
        setCapability(CapabilityState::Unsupported,
                      QStringLiteral("ни login1, ни хранитель экрана недоступны"));
    }

    qCInfo(logSession, "сон: %s, блокировка: %s",
           sleep ? "да" : "нет", lock ? "да" : "нет");
}

void SessionAdapter::onPrepareForSleep(bool goingToSleep)
{
    // Событие приходит и перед засыпанием, и после пробуждения — это один
    // сигнал с разным аргументом, а не два разных.
    emit sessionChanged(goingToSleep ? State::Sleeping : State::Resumed);
}

void SessionAdapter::onScreensaverActiveChanged(bool active)
{
    emit sessionChanged(active ? State::Locked : State::Resumed);
}
