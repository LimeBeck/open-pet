#pragma once

#include "eventsource.h"

// Сон, возобновление и блокировка экрана (§FR-3).
//
// Два независимых источника: `org.freedesktop.login1` на системной шине
// сообщает о засыпании и пробуждении, хранитель экрана на сессионной —
// о блокировке. Один из них может быть недоступен без второго, поэтому
// capability отражает худшее из двух.
class SessionAdapter : public EventSource
{
    Q_OBJECT

public:
    enum class State {
        Active,
        Locked,
        Sleeping,
        Resumed,
    };
    Q_ENUM(State)

    explicit SessionAdapter(QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("session"); }
    void start() override;

signals:
    void sessionChanged(SessionAdapter::State state);

private slots:
    // login1 шлёт `true` перед засыпанием и `false` после пробуждения.
    void onPrepareForSleep(bool goingToSleep);
    void onScreensaverActiveChanged(bool active);

private:
    bool connectSleep();
    bool connectLock();
};
