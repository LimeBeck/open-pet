#include "mediaadapter.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#include <QLoggingCategory>
#include <QStringList>

Q_LOGGING_CATEGORY(logMedia, "openpet.media")

namespace {

constexpr auto kPlayerPath = "/org/mpris/MediaPlayer2";
constexpr auto kPlayerInterface = "org.mpris.MediaPlayer2.Player";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";
const QString kMprisPrefix = QStringLiteral("org.mpris.MediaPlayer2.");

QString readPlaybackStatus(const QString &service)
{
    QDBusInterface properties(service,
                              QString::fromLatin1(kPlayerPath),
                              QString::fromLatin1(kPropertiesInterface),
                              QDBusConnection::sessionBus());
    if (!properties.isValid())
        return {};

    // Ровно одно свойство. Get вместо GetAll намеренно: GetAll вернул бы
    // и Metadata с названием трека, которое нам знать не положено (§4.2).
    const QDBusReply<QDBusVariant> reply = properties.call(QStringLiteral("Get"),
                                                           QString::fromLatin1(kPlayerInterface),
                                                           QStringLiteral("PlaybackStatus"));
    return reply.isValid() ? reply.value().variant().toString() : QString();
}

} // namespace

MediaAdapter::MediaAdapter(QObject *parent)
    : EventSource(parent)
{
}

void MediaAdapter::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        setCapability(CapabilityState::Unsupported,
                      QStringLiteral("сессионная шина D-Bus недоступна"));
        return;
    }

    QDBusInterface dbus(QStringLiteral("org.freedesktop.DBus"),
                        QStringLiteral("/org/freedesktop/DBus"),
                        QStringLiteral("org.freedesktop.DBus"),
                        bus);
    const QDBusReply<QStringList> names = dbus.call(QStringLiteral("ListNames"));
    if (!names.isValid()) {
        setCapability(CapabilityState::Unsupported, QStringLiteral("список имён шины недоступен"));
        return;
    }

    for (const QString &service : names.value()) {
        if (service.startsWith(kMprisPrefix))
            watchPlayer(service);
    }

    // Плееры появляются и исчезают по ходу работы, поэтому одного обхода
    // при старте мало.
    const bool watchingOwners = bus.connect(QStringLiteral("org.freedesktop.DBus"),
                                            QStringLiteral("/org/freedesktop/DBus"),
                                            QStringLiteral("org.freedesktop.DBus"),
                                            QStringLiteral("NameOwnerChanged"),
                                            this,
                                            SLOT(onNameOwnerChanged(QString, QString, QString)));

    setCapability(watchingOwners ? CapabilityState::Available : CapabilityState::Degraded,
                  watchingOwners ? QString()
                                 : QStringLiteral("новые плееры не будут замечены"));

    qCInfo(logMedia, "плееров при старте: %d", int(m_players.size()));
    publishIfChanged();
}

void MediaAdapter::watchPlayer(const QString &service)
{
    if (m_players.contains(service))
        return;

    QDBusConnection::sessionBus().connect(service,
                                          QString::fromLatin1(kPlayerPath),
                                          QString::fromLatin1(kPropertiesInterface),
                                          QStringLiteral("PropertiesChanged"),
                                          this,
                                          SLOT(onPlayerPropertiesChanged()));
    m_players.insert(service);
}

void MediaAdapter::unwatchPlayer(const QString &service)
{
    if (!m_players.remove(service))
        return;

    QDBusConnection::sessionBus().disconnect(service,
                                             QString::fromLatin1(kPlayerPath),
                                             QString::fromLatin1(kPropertiesInterface),
                                             QStringLiteral("PropertiesChanged"),
                                             this,
                                             SLOT(onPlayerPropertiesChanged()));
}

void MediaAdapter::onNameOwnerChanged(const QString &service,
                                      const QString &oldOwner,
                                      const QString &newOwner)
{
    if (!service.startsWith(kMprisPrefix))
        return;

    if (newOwner.isEmpty())
        unwatchPlayer(service);
    else if (oldOwner.isEmpty())
        watchPlayer(service);

    publishIfChanged();
}

void MediaAdapter::onPlayerPropertiesChanged()
{
    // Аргументы сигнала намеренно не объявлены и не читаются: в них лежит
    // Metadata с названием трека. Сигнал нужен только как повод перечитать
    // PlaybackStatus.
    publishIfChanged();
}

MediaAdapter::State MediaAdapter::readAggregateState() const
{
    bool anyPaused = false;

    for (const QString &service : m_players) {
        const QString status = readPlaybackStatus(service);
        if (status == QLatin1String("Playing")) {
            // Достаточно одного играющего плеера: питомец не разбирается,
            // какой именно из них звучит.
            return State::Playing;
        }
        if (status == QLatin1String("Paused"))
            anyPaused = true;
    }

    return anyPaused ? State::Paused : State::Stopped;
}

void MediaAdapter::publishIfChanged()
{
    const State state = readAggregateState();

    if (m_hasLast && m_lastState == state)
        return;

    m_hasLast = true;
    m_lastState = state;

    qCDebug(logMedia, "воспроизведение: %s",
            state == State::Playing  ? "идёт"
                : state == State::Paused ? "пауза"
                                         : "остановлено");

    emit mediaChanged(state);
}
