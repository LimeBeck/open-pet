#include "mockeventsource.h"

#include "corebridge.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(logMock, "openpet.mock")

MockEventSource::MockEventSource(CoreBridge *core, QObject *parent)
    : QObject(parent)
    , m_core(core)
{
    connect(&m_timer, &QTimer::timeout, this, &MockEventSource::step);
}

// Интервал должен превышать самый долгий ttl сценария (8 секунд у low_battery),
// иначе следующее состояние окажется подавлено предыдущим и часть сценария
// никогда не покажется. На 6 секундах так и произошло: состояние sleepy
// не появлялось ни разу за прогон.
void MockEventSource::start(int intervalMs)
{
    if (!m_core)
        return;

    qCInfo(logMock, "источники событий — заглушка M1; настоящие адаптеры в M3–M4");
    m_timer.start(intervalMs);
}

void MockEventSource::stop()
{
    m_timer.stop();
}

void MockEventSource::step()
{
    if (!m_core)
        return;

    // Сценарий подобран так, чтобы по кругу проходить все восемь состояний:
    // скелет считается рабочим, только если видно каждое из них (§13, п. 2).
    switch (m_index) {
    case 0:
        m_core->pushActiveAppChanged(QStringLiteral("org.kde.konsole"));
        break;
    case 1:
        m_core->pushNotification(QStringLiteral("im"));
        break;
    case 2:
        m_core->pushMediaChanged(OPENPET_MEDIA_PLAYING);
        break;
    case 3:
        m_core->pushPowerChanged(false, 62, OPENPET_POWER_CHARGING);
        break;
    case 4:
        m_core->pushPowerChanged(true, 8, OPENPET_POWER_DISCHARGING);
        break;
    case 5:
        m_core->pushIdleThreshold(300);
        break;
    case 6:
        m_core->pushActivityResumed();
        break;
    default:
        m_core->pushMediaChanged(OPENPET_MEDIA_STOPPED);
        break;
    }

    m_index = (m_index + 1) % 8;
}
