#include "idleadapter.h"

#include <KIdleTime>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(logIdle, "openpet.idle")

IdleAdapter::IdleAdapter(int thresholdSeconds, QObject *parent)
    : EventSource(parent)
    , m_thresholdSeconds(qMax(5, thresholdSeconds))
{
}

void IdleAdapter::start()
{
    KIdleTime *idle = KIdleTime::instance();
    if (!idle) {
        setCapability(CapabilityState::Unsupported, QStringLiteral("KIdleTime недоступен"));
        return;
    }

    m_timeoutId = idle->addIdleTimeout(m_thresholdSeconds * 1000);

    connect(idle, &KIdleTime::timeoutReached, this, [this](int identifier) {
        if (identifier != m_timeoutId)
            return;

        emit idleThresholdReached(quint32(m_thresholdSeconds));

        // Без этого возвращение пользователя останется незамеченным:
        // KIdleTime сообщает о выходе из простоя только по явной подписке,
        // и подписываться нужно заново после каждого срабатывания.
        KIdleTime::instance()->catchNextResumeEvent();
    });

    connect(idle, &KIdleTime::resumingFromIdle, this, [this] { emit activityResumed(); });

    setCapability(CapabilityState::Available);
    qCInfo(logIdle, "порог простоя: %d с", m_thresholdSeconds);
}
