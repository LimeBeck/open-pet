#include "settingscontroller.h"

#include "autostart.h"
#include "corebridge.h"
#include "llmclient.h"
#include "secretstore.h"
#include "sheetquality.h"

#include <QFile>
#include <QImage>
#include <QStandardPaths>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(logApp)

namespace {

const QString kApiKeyAccount = QStringLiteral("llm-api-key");
const QString kProxyPasswordAccount = QStringLiteral("proxy-password");

QString cornerToString(OverlaySurface::Corner corner)
{
    switch (corner) {
    case OverlaySurface::Corner::BottomLeft:
        return QStringLiteral("bottom-left");
    case OverlaySurface::Corner::TopRight:
        return QStringLiteral("top-right");
    case OverlaySurface::Corner::TopLeft:
        return QStringLiteral("top-left");
    case OverlaySurface::Corner::BottomRight:
        break;
    }
    return QStringLiteral("bottom-right");
}

OverlaySurface::Corner cornerFromString(const QString &value)
{
    if (value == QLatin1String("bottom-left"))
        return OverlaySurface::Corner::BottomLeft;
    if (value == QLatin1String("top-right"))
        return OverlaySurface::Corner::TopRight;
    if (value == QLatin1String("top-left"))
        return OverlaySurface::Corner::TopLeft;
    return OverlaySurface::Corner::BottomRight;
}

} // namespace

SettingsController::SettingsController(CoreBridge *core, LlmClient *llm, QObject *parent)
    : QObject(parent)
    , m_core(core)
    , m_llm(llm)
    , m_settings(Settings::load())
{
    if (m_llm) {
        connect(m_llm, &LlmClient::healthChecked, this,
                [this](bool ok, bool modelFound, const QString &detail) {
                    // Формулировки разные не для красоты: «нет связи»
                    // и «связь есть, модели нет» чинятся по-разному.
                    m_healthStatus = ok && modelFound ? tr("✓ %1").arg(detail)
                        : ok                          ? tr("⚠ %1").arg(detail)
                                                      : tr("✗ %1").arg(detail);
                    emit changed();
                });
    }

    // Наличие ключа проверяется, само значение не читается: незачем держать
    // секрет в памяти, пока он не понадобился.
    m_hasApiKey = SecretStore::isAvailable() && !SecretStore::read(kApiKeyAccount).isEmpty();
}

QString SettingsController::corner() const
{
    return cornerToString(m_settings.corner);
}

void SettingsController::markDirty(bool needsRestart)
{
    if (needsRestart) {
        // Часть настроек применяется только при следующем запуске.
        // Говорить об этом честно лучше, чем делать вид, что всё применилось.
        m_restartNotice =
            tr("Положение, отступы и источники событий применятся после перезапуска");
    }
    emit changed();
}

void SettingsController::setCorner(const QString &corner)
{
    const auto value = cornerFromString(corner);
    if (m_settings.corner == value)
        return;
    m_settings.corner = value;
    markDirty(true);
}

void SettingsController::setMarginRight(int value)
{
    value = qBound(0, value, 4000);
    if (m_settings.marginRight == value)
        return;
    m_settings.marginRight = value;
    markDirty(true);
}

void SettingsController::setMarginBottom(int value)
{
    value = qBound(0, value, 4000);
    if (m_settings.marginBottom == value)
        return;
    m_settings.marginBottom = value;
    markDirty(true);
}

void SettingsController::setScale(qreal value)
{
    // Диапазон §FR-1: 75–200%.
    value = qBound(0.75, value, 2.0);
    if (qFuzzyCompare(m_settings.scale, value))
        return;
    m_settings.scale = value;
    markDirty();
}

void SettingsController::setReducedMotion(bool value)
{
    if (m_settings.reducedMotion == value)
        return;
    m_settings.reducedMotion = value;
    markDirty();
}

void SettingsController::setPaused(bool value)
{
    if (m_settings.paused == value)
        return;
    m_settings.paused = value;
    markDirty();
}

void SettingsController::setIdleSeconds(int value)
{
    value = qBound(5, value, 3600);
    if (m_settings.idleSeconds == value)
        return;
    m_settings.idleSeconds = value;
    markDirty(true);
}

#define OPENPET_SOURCE_SETTER(Name, Field)                                                         \
    void SettingsController::set##Name(bool value)                                                 \
    {                                                                                              \
        if (m_settings.Field == value)                                                             \
            return;                                                                                \
        m_settings.Field = value;                                                                  \
        markDirty(true);                                                                           \
    }

OPENPET_SOURCE_SETTER(SourceIdle, sourceIdle)
OPENPET_SOURCE_SETTER(SourcePower, sourcePower)
OPENPET_SOURCE_SETTER(SourceSession, sourceSession)
OPENPET_SOURCE_SETTER(SourceMedia, sourceMedia)
OPENPET_SOURCE_SETTER(SourceNotification, sourceNotification)
OPENPET_SOURCE_SETTER(SourceActiveApp, sourceActiveApp)

#undef OPENPET_SOURCE_SETTER

void SettingsController::setLlmKind(int value)
{
    value = qBound(0, value, 4);
    if (m_settings.llmKind == value)
        return;
    m_settings.llmKind = value;
    markDirty();
}

void SettingsController::setLlmBaseUrl(const QString &value)
{
    if (m_settings.llmBaseUrl == value)
        return;
    m_settings.llmBaseUrl = value;
    markDirty();
}

void SettingsController::setLlmModel(const QString &value)
{
    if (m_settings.llmModel == value)
        return;
    m_settings.llmModel = value;
    markDirty();
}

void SettingsController::setLlmProject(const QString &value)
{
    if (m_settings.llmProject == value)
        return;
    m_settings.llmProject = value;
    markDirty();
}

void SettingsController::setLlmRegion(const QString &value)
{
    if (m_settings.llmRegion == value)
        return;
    m_settings.llmRegion = value;
    markDirty();
}

QString SettingsController::googleCredentialsPath() const
{
    return qEnvironmentVariableIsSet("OPENPET_GOOGLE_ADC")
        ? qEnvironmentVariable("OPENPET_GOOGLE_ADC")
        : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/gcloud/application_default_credentials.json");
}

bool SettingsController::googleCredentialsFound() const
{
    return QFile::exists(googleCredentialsPath());
}

void SettingsController::setLlmTimeoutMs(int value)
{
    value = qBound(500, value, 60000);
    if (m_settings.llmTimeoutMs == value)
        return;
    m_settings.llmTimeoutMs = value;
    markDirty();
}

void SettingsController::setProxyMode(int value)
{
    value = qBound(0, value, 2);
    if (m_settings.proxyMode == value)
        return;
    m_settings.proxyMode = value;
    markDirty();
}

void SettingsController::setProxyHost(const QString &value)
{
    if (m_settings.proxyHost == value)
        return;
    m_settings.proxyHost = value;
    markDirty();
}

void SettingsController::setProxyPort(int value)
{
    value = qBound(0, value, 65535);
    if (m_settings.proxyPort == value)
        return;
    m_settings.proxyPort = value;
    markDirty();
}

void SettingsController::setProxyUser(const QString &value)
{
    if (m_settings.proxyUser == value)
        return;
    m_settings.proxyUser = value;
    markDirty();
}

void SettingsController::setProxyBypassLocal(bool value)
{
    if (m_settings.proxyBypassLocal == value)
        return;
    m_settings.proxyBypassLocal = value;
    markDirty();
}

bool SettingsController::storeProxyPassword(const QString &password)
{
    const bool stored = SecretStore::store(kProxyPasswordAccount, password);
    emit changed();
    return stored;
}

QString SettingsController::activePackId() const
{
    return m_core ? m_core->activePackId() : QString();
}

void SettingsController::importPack(const QUrl &fileUrl)
{
    if (!m_core)
        return;

    QFile file(fileUrl.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        m_packStatus = tr("✗ файл не открывается");
        emit changed();
        return;
    }

    // Размер ограничен до чтения, а не после: смысл лимита в том, чтобы
    // не втягивать в память гигабайтный файл, а не в том, чтобы отвергнуть
    // его, уже втянув.
    constexpr qint64 kMaxArchiveBytes = 64 * 1024 * 1024;
    if (file.size() > kMaxArchiveBytes) {
        m_packStatus = tr("✗ файл больше 64 МиБ");
        emit changed();
        return;
    }

    const QByteArray archive = file.readAll();
    file.close();

    QString sheetPath;
    const CoreBridge::PackInstall result = m_core->installPack(archive, &sheetPath);

    if (!result.accepted) {
        // Активный пакет не тронут: ядро подменяет его только после того,
        // как проверка пройдена (§10).
        m_packStatus = tr("✗ пакет отклонён:\n%1").arg(result.report);
        emit changed();
        return;
    }

    // Качество листа проверяется после установки, а не вместо неё: пакет
    // с дрожащими кадрами исправен, просто нарисован неаккуратно, и решать
    // тут пользователю (ADR-005).
    QStringList quality;
    if (!sheetPath.isEmpty()) {
        static const QStringList kStates { QStringLiteral("idle"),        QStringLiteral("happy"),
                                           QStringLiteral("curious"),     QStringLiteral("sleepy"),
                                           QStringLiteral("charging"),    QStringLiteral("low_battery"),
                                           QStringLiteral("notification"), QStringLiteral("busy") };

        QList<CoreBridge::Animation> layout;
        layout.reserve(kStates.size());
        for (const QString &state : kStates)
            layout.append(m_core->animationFor(state));

        quality = SheetQuality::inspect(QImage(sheetPath), layout, kStates);
    }

    // Выбор запоминается: §9 требует хранить активный пакет между запусками.
    m_settings.activePackId = activePackId();
    m_settings.save();

    QStringList notes;
    if (!result.report.isEmpty())
        notes << result.report;
    notes << quality;

    m_packStatus = notes.isEmpty()
        ? tr("✓ установлен: %1").arg(activePackId())
        : tr("✓ установлен: %1\nзамечания:\n%2").arg(activePackId(), notes.join(QLatin1Char('\n')));

    emit petPackChanged(sheetPath);
    emit changed();
}

void SettingsController::resetPackToBuiltin()
{
    if (!m_core)
        return;

    m_core->rollbackPack();
    m_settings.activePackId = activePackId();
    m_settings.save();
    m_packStatus = tr("вернулись к пакету: %1").arg(activePackId());
    emit petPackChanged(QString());
    emit changed();
}

void SettingsController::checkConnection()
{
    if (!m_llm)
        return;

    // Настройки применяются к ядру до проверки: иначе проверялся бы
    // провайдер, настроенный в прошлый раз, а не тот, что в окне.
    apply();

    m_healthStatus = tr("проверяю…");
    emit changed();

    m_llm->checkHealth();
}

bool SettingsController::autostart() const
{
    return Autostart::isEnabled();
}

void SettingsController::setAutostart(bool value)
{
    if (Autostart::isEnabled() == value)
        return;

    if (!Autostart::setEnabled(value)) {
        m_restartNotice = value ? tr("Не удалось включить автозапуск: файл не записан")
                                : tr("Не удалось выключить автозапуск: файл не удалён");
    }

    emit changed();
}

bool SettingsController::secretStorageAvailable() const
{
    return SecretStore::isAvailable();
}

QString SettingsController::payloadPreview() const
{
    if (!m_core || m_settings.llmKind == 0)
        return {};

    // Настройки применяются к ядру временно, чтобы показать настоящее тело
    // запроса, а не его описание. Пользователь видит ровно то, что уйдёт.
    m_core->setLlmProvider(m_settings.llmKind, m_settings.llmBaseUrl, m_settings.llmModel,
                           m_settings.llmProject, m_settings.llmRegion);

    CoreBridge::LlmRequest plan;
    if (!m_core->buildLlmRequest(&plan)) {
        return tr("Пример появится после первой реакции питомца: тело запроса "
                  "строится из неё.");
    }

    return plan.url + QStringLiteral("\n\n") + plan.body;
}

bool SettingsController::storeApiKey(const QString &key)
{
    const bool stored = SecretStore::store(kApiKeyAccount, key);
    m_hasApiKey = stored && !key.isEmpty();
    emit changed();
    return stored;
}

bool SettingsController::forgetApiKey()
{
    const bool removed = SecretStore::remove(kApiKeyAccount);
    m_hasApiKey = false;
    emit changed();
    return removed;
}

void SettingsController::apply()
{
    m_settings.save();

    if (m_core) {
        m_core->setPaused(m_settings.paused);
        // Выключенный провайдер означает ноль сетевых запросов (§7),
        // поэтому ядру сообщается именно 0, а не «настроен, но не используется».
        m_core->setLlmProvider(m_settings.llmKind, m_settings.llmBaseUrl, m_settings.llmModel,
                               m_settings.llmProject, m_settings.llmRegion);
    }

    emit applied();
}

void SettingsController::resetLocalData()
{
    Settings::resetLocalData();

    if (m_core)
        m_core->clearPhraseHistory();

    m_settings = Settings::load();
    m_restartNotice = tr("Локальные данные удалены. Импортированные питомцы остались.");

    emit changed();
    emit localDataReset();
}
