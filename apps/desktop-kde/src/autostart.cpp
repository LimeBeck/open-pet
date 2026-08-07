#include "autostart.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QTextStream>

Q_LOGGING_CATEGORY(logAutostart, "openpet.autostart")

namespace {

const QString kFileName = QStringLiteral("org.openpet.DesktopPet.desktop");

QString autostartDir()
{
    // XDG_CONFIG_HOME/autostart — общий для всех сред каталог. Plasma читает
    // его наравне со своими, поэтому отдельной интеграции с KDE не нужно.
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QStringLiteral("/autostart");
}

QString autostartPath()
{
    return autostartDir() + QLatin1Char('/') + kFileName;
}

} // namespace

bool Autostart::isEnabled()
{
    return QFile::exists(autostartPath());
}

bool Autostart::setEnabled(bool enabled)
{
    const QString path = autostartPath();

    if (!enabled) {
        if (!QFile::exists(path))
            return true;

        if (!QFile::remove(path)) {
            qCWarning(logAutostart, "не удалось удалить файл автозапуска");
            return false;
        }
        return true;
    }

    if (!QDir().mkpath(autostartDir())) {
        qCWarning(logAutostart, "не удалось создать каталог автозапуска");
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qCWarning(logAutostart, "не удалось записать файл автозапуска");
        return false;
    }

    // Путь берётся у запущенного процесса, а не из константы: приложение может
    // быть установлено в префикс пользователя или запускаться из сборки,
    // и записанный жёстко /usr/bin просто не существовал бы.
    const QString executable = QCoreApplication::applicationFilePath();

    QTextStream out(&file);
    out << "[Desktop Entry]\n"
        << "Type=Application\n"
        << "Name=open-pet\n"
        << "Comment=Desktop AI Pet\n"
        << "Exec=" << executable << "\n"
        << "Icon=org.openpet.DesktopPet\n"
        << "Terminal=false\n"
        << "X-GNOME-Autostart-enabled=true\n"
        // Питомец живёт в layer-shell поверхности и вне Plasma не запустится.
        // Автозапуск в чужой среде показал бы пользователю только ошибку.
        << "OnlyShowIn=KDE;\n";

    if (out.status() != QTextStream::Ok || !file.flush()) {
        qCWarning(logAutostart, "файл автозапуска записан не полностью");
        file.remove();
        return false;
    }

    return true;
}
