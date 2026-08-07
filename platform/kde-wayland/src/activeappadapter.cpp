#include "activeappadapter.h"

#include <QDBusConnection>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(logActiveApp, "openpet.activeapp")

namespace {

constexpr auto kServiceName = "org.openpet.DesktopPet";
constexpr auto kObjectPath = "/org/openpet/DesktopPet";

} // namespace

ActiveAppAdapter::ActiveAppAdapter(QObject *parent)
    : EventSource(parent)
{
}

QString ActiveAppAdapter::scriptInstallPath()
{
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + QStringLiteral("/kwin/scripts/openpet-active-window");
}

bool ActiveAppAdapter::isScriptInstalled()
{
    return QFileInfo::exists(scriptInstallPath() + QStringLiteral("/metadata.json"));
}

void ActiveAppAdapter::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        setCapability(CapabilityState::Unsupported,
                      QStringLiteral("сессионная шина D-Bus недоступна"));
        return;
    }

    if (!bus.registerService(QString::fromLatin1(kServiceName))) {
        // Имя уже занято — обычно это второй запущенный экземпляр приложения.
        setCapability(CapabilityState::Unsupported,
                      QStringLiteral("имя D-Bus занято, вероятно уже запущен другой экземпляр"));
        return;
    }

    if (!bus.registerObject(QString::fromLatin1(kObjectPath),
                            this,
                            QDBusConnection::ExportScriptableSlots)) {
        setCapability(CapabilityState::Unsupported,
                      QStringLiteral("не удалось опубликовать объект D-Bus"));
        return;
    }

    if (!isScriptInstalled()) {
        // Сервис поднят и готов принимать, но присылать некому. Это именно
        // permission_required: нужно осознанное действие пользователя,
        // а не исправление ошибки (§10).
        setCapability(CapabilityState::PermissionRequired,
                      QStringLiteral("KWin-скрипт не установлен"));
        return;
    }

    setCapability(CapabilityState::Available);
}

void ActiveAppAdapter::SetActiveApp(const QString &appId)
{
    // Длина ограничена, чтобы через этот вход нельзя было прислать что-то
    // объёмное: сюда приходит идентификатор приложения, а не текст.
    if (appId.isEmpty() || appId.size() > 128)
        return;

    if (appId == m_lastAppId)
        return;

    m_lastAppId = appId;

    // Идентификатор не пишется в журнал: сам по себе он безобиден, но
    // последовательность таких записей — это история приложений пользователя,
    // а её §9 хранить запрещает.
    qCDebug(logActiveApp, "активное приложение сменилось");

    emit activeAppChanged(appId);
}
