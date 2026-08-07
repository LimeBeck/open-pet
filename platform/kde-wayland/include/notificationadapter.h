#pragma once

#include "eventsource.h"

// Факт появления уведомления (§FR-3, [ADR-004](../../../docs/adr/0004-notification-observation.md)).
//
// Наблюдается сигнал `NotificationClosed` штатного сервера уведомлений.
// Он вещается всей шине без указания получателя, поэтому подписка не требует
// ни прав монитора, ни особых политик D-Bus.
//
// В теле сигнала два числа: идентификатор уведомления и причина закрытия.
// Ни текста, ни отправителя, ни категории там нет — не потому, что мы их
// отбрасываем, а потому, что их туда не кладут. Это и есть та граница,
// которую требует §4.2.
//
// Цена: сигнал приходит при **закрытии**, а не при появлении. Уведомление,
// закрывшееся по таймауту, даёт событие через несколько секунд после того,
// как его показали. Сигнала о появлении в протоколе нет вовсе: `Notify` —
// это вызов метода, который видит только сам сервер.
class NotificationAdapter : public EventSource
{
    Q_OBJECT

public:
    explicit NotificationAdapter(QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("notification"); }
    void start() override;

signals:
    // Категории нет и не будет: сигнал её не содержит.
    void notificationOccurred();

private slots:
    void onNotificationClosed(quint32 id, quint32 reason);
};
