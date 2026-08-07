#include "poweradapter.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(logPower, "openpet.power")

namespace {

constexpr auto kService = "org.freedesktop.UPower";
constexpr auto kDisplayDevicePath = "/org/freedesktop/UPower/devices/DisplayDevice";
constexpr auto kDeviceInterface = "org.freedesktop.UPower.Device";
constexpr auto kPropertiesInterface = "org.freedesktop.DBus.Properties";

// Коды состояния UPower. Дальше этого файла они не уходят.
constexpr int kUPowerCharging = 1;
constexpr int kUPowerDischarging = 2;
constexpr int kUPowerFullyCharged = 4;

PowerAdapter::Kind kindFromUPower(int state)
{
    switch (state) {
    case kUPowerCharging:
        return PowerAdapter::Kind::Charging;
    case kUPowerDischarging:
        return PowerAdapter::Kind::Discharging;
    case kUPowerFullyCharged:
        return PowerAdapter::Kind::Full;
    default:
        // Pending charge, pending discharge и empty для питомца неотличимы
        // от «неизвестно»: реакции на них всё равно нет.
        return PowerAdapter::Kind::Unknown;
    }
}

QVariant readProperty(const QString &name)
{
    QDBusInterface properties(QString::fromLatin1(kService),
                              QString::fromLatin1(kDisplayDevicePath),
                              QString::fromLatin1(kPropertiesInterface),
                              QDBusConnection::systemBus());
    if (!properties.isValid())
        return {};

    const QDBusReply<QDBusVariant> reply =
        properties.call(QStringLiteral("Get"), QString::fromLatin1(kDeviceInterface), name);
    return reply.isValid() ? reply.value().variant() : QVariant();
}

} // namespace

PowerAdapter::PowerAdapter(QObject *parent)
    : EventSource(parent)
{
}

void PowerAdapter::start()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        setCapability(CapabilityState::Unsupported,
                      QStringLiteral("системная шина D-Bus недоступна"));
        return;
    }

    const QVariant state = readProperty(QStringLiteral("State"));
    if (!state.isValid()) {
        // UPower не установлен или не отвечает: правила питания просто
        // не будут срабатывать, остальное приложение работает (§10).
        setCapability(CapabilityState::Unsupported, QStringLiteral("UPower не отвечает"));
        return;
    }

    const bool connected = bus.connect(QString::fromLatin1(kService),
                                       QString::fromLatin1(kDisplayDevicePath),
                                       QString::fromLatin1(kPropertiesInterface),
                                       QStringLiteral("PropertiesChanged"),
                                       this,
                                       SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));

    if (!connected) {
        // Читать свойства можем, а узнавать об изменениях — нет. Это именно
        // degraded: событие придёт только при следующем опросе, которого нет.
        setCapability(CapabilityState::Degraded,
                      QStringLiteral("подписка на PropertiesChanged не удалась"));
    } else {
        setCapability(CapabilityState::Available);
    }

    publishIfChanged();
}

void PowerAdapter::onPropertiesChanged(const QString &interface,
                                       const QVariantMap &changed,
                                       const QStringList &invalidated)
{
    Q_UNUSED(changed)
    Q_UNUSED(invalidated)

    if (interface != QLatin1String(kDeviceInterface))
        return;

    publishIfChanged();
}

void PowerAdapter::publishIfChanged()
{
    const QVariant stateValue = readProperty(QStringLiteral("State"));
    if (!stateValue.isValid())
        return;

    const PowerAdapter::Kind kind = kindFromUPower(stateValue.toInt());
    const QVariant percentValue = readProperty(QStringLiteral("Percentage"));
    const int percent = percentValue.isValid() ? int(percentValue.toDouble() + 0.5) : -1;

    const bool onBattery = kind == PowerAdapter::Kind::Discharging;

    // UPower шлёт PropertiesChanged на каждый процент заряда. Пропускаем
    // повторы, чтобы поток событий не упирался в cooldown ядра впустую
    // (§10, «частый поток событий»).
    const bool sameAsBefore = m_hasLast && m_lastOnBattery == onBattery
        && m_lastPercent == percent && m_lastKind == kind;
    if (sameAsBefore)
        return;

    m_hasLast = true;
    m_lastOnBattery = onBattery;
    m_lastPercent = percent;
    m_lastKind = kind;

    qCDebug(logPower, "питание: %s, заряд %d%%",
            onBattery ? "от батареи" : "от сети", percent);

    emit powerChanged(onBattery, percent, kind);
}
