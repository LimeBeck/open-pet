#pragma once

#include <QObject>
#include <QString>

// Состояние здоровья источника событий (§FR-4).
//
// Недоступность источника — штатная ситуация, а не ошибка: приложение
// продолжает работать без соответствующей capability (§10). Поэтому
// состояние публикуется наружу, а не приводит к отказу запуска.
enum class CapabilityState {
    Available,
    PermissionRequired,
    Unsupported,
    Degraded,
};

QString capabilityStateName(CapabilityState state);

// Общий контракт адаптера системных событий.
//
// Адаптер переводит платформенное событие в нормализованное (§FR-4)
// и ничего не знает ни о ядре, ни о правилах поведения. Всё, что он
// вправе сообщить, перечислено в сигналах: расширение этого списка —
// расширение того, что приложение вообще способно наблюдать.
class EventSource : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // Устойчивое имя для диагностики и настроек. Не переводится.
    virtual QString name() const = 0;

    CapabilityState capability() const { return m_capability; }

    // Подключается к источнику. Вызывать один раз; неудача не является
    // ошибкой запуска — она меняет capability.
    virtual void start() = 0;

signals:
    void capabilityChanged(CapabilityState state, const QString &reason);

protected:
    // Причина нужна для журнала: «unsupported» без объяснения превращает
    // разбор жалоб в гадание.
    void setCapability(CapabilityState state, const QString &reason = {});

private:
    CapabilityState m_capability = CapabilityState::Unsupported;
};
