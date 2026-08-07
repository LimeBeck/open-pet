#pragma once

#include "eventsource.h"

// Простой и возвращение к работе через KIdleTime (§FR-3).
//
// Приложение узнаёт **только** время бездействия и переход idle ↔ active.
// Ни клавиш, ни координат, ни частоты нажатий здесь нет и быть не может —
// KIdleTime такого и не отдаёт (§4.2).
class IdleAdapter : public EventSource
{
    Q_OBJECT

public:
    explicit IdleAdapter(int thresholdSeconds, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("idle"); }
    void start() override;

    int thresholdSeconds() const { return m_thresholdSeconds; }

signals:
    void idleThresholdReached(quint32 seconds);
    void activityResumed();

private:
    int m_thresholdSeconds;
    int m_timeoutId = -1;
};
