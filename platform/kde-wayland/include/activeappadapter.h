#pragma once

#include "eventsource.h"

// Смена активного приложения (§FR-3, [ADR-003](../../../docs/adr/0003-kwin-integration.md)).
//
// Обычный клиент Wayland не наблюдает чужие окна, поэтому наблюдатель живёт
// внутри композитора: отдельный KWin-скрипт, который пользователь ставит
// осознанно. Здесь только приёмная сторона — сервис D-Bus, куда скрипт
// присылает идентификатор.
//
// Приходит **только** `resourceClass` вида `org.kde.konsole`. Заголовок окна
// не передаётся и не должен появиться: в нём имя открытого документа (§4.2).
class ActiveAppAdapter : public EventSource
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.openpet.DesktopPet")

public:
    explicit ActiveAppAdapter(QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("active_app"); }
    void start() override;

    // Установлен ли скрипт в каталог пользователя. Установка — отдельное
    // действие: скрипт ставится осознанно, а не молча при первом запуске.
    static bool isScriptInstalled();
    static QString scriptInstallPath();

public slots:
    // Вызывается KWin-скриптом. Публичный слот — это и есть метод D-Bus.
    Q_SCRIPTABLE void SetActiveApp(const QString &appId);

signals:
    void activeAppChanged(const QString &appId);

private:
    QString m_lastAppId;
};
