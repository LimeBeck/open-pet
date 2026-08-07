#pragma once

#include "eventsource.h"

#include <QSet>
#include <QString>

// Воспроизведение медиа через MPRIS (§FR-3).
//
// Читается **только** свойство `PlaybackStatus` — три значения из §4.2.
// Ни названия трека, ни исполнителя, ни обложки: `Metadata` не запрашивается
// никогда.
//
// Сигнал `PropertiesChanged` от плеера приносит Metadata в своём теле,
// хотим мы того или нет. Поэтому сигнал используется только как повод
// перечитать одно разрешённое свойство, а его содержимое не разбирается —
// тот же приём, что в адаптере питания.
class MediaAdapter : public EventSource
{
    Q_OBJECT

public:
    enum class State {
        Stopped,
        Playing,
        Paused,
    };
    Q_ENUM(State)

    explicit MediaAdapter(QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("media"); }
    void start() override;

signals:
    void mediaChanged(MediaAdapter::State state);

private slots:
    void onNameOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void onPlayerPropertiesChanged();

private:
    void watchPlayer(const QString &service);
    void unwatchPlayer(const QString &service);
    void publishIfChanged();
    State readAggregateState() const;

    QSet<QString> m_players;
    State m_lastState = State::Stopped;
    bool m_hasLast = false;
};
