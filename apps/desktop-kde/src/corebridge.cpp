#include "corebridge.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <QMetaObject>

Q_LOGGING_CATEGORY(logCore, "openpet.core")

namespace {

// Инициализация нулями обязательна: структура события плоская, и поля,
// не относящиеся к текущему kind, ядро всё равно прочитает (ADR-001, правило 2).
OpenPetEvent makeEvent(OpenPetEventKind kind)
{
    OpenPetEvent event {};
    event.kind = kind;
    return event;
}

CoreBridge::Reaction fromFfi(const OpenPetReaction &raw)
{
    CoreBridge::Reaction reaction;
    reaction.emotion = int(raw.emotion);
    reaction.animation = int(raw.animation);
    reaction.priority = int(raw.priority);
    reaction.ttlMs = int(raw.ttl_ms);
    reaction.cooldownKey = QString::fromUtf8(raw.cooldown_key);
    if (raw.has_phrase)
        reaction.phrase = QString::fromUtf8(raw.phrase);
    return reaction;
}

} // namespace

CoreBridge::CoreBridge(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<CoreBridge::Reaction>();

    if (openpet_abi_version() != OPENPET_ABI_VERSION) {
        // Ядро и хост собраны из разных версий контракта: раскладка структур
        // могла измениться, и чтение полей даст мусор (ADR-001, правило 5).
        qCCritical(logCore, "несовпадение ABI: хост ждёт %d, ядро сообщает %u",
                   OPENPET_ABI_VERSION, openpet_abi_version());
        return;
    }

    m_core = openpet_core_new();
    if (!m_core) {
        qCCritical(logCore, "не удалось создать ядро");
        return;
    }

    openpet_core_set_reaction_callback(m_core, &CoreBridge::onReaction, this);
    openpet_core_set_log_callback(m_core, &CoreBridge::onLog, this);
}

CoreBridge::~CoreBridge()
{
    if (!m_core)
        return;

    // Сначала снимаем callback, потом освобождаем ядро: иначе фоновый поток
    // успеет позвать нас уже после начала разрушения объекта.
    openpet_core_set_reaction_callback(m_core, nullptr, nullptr);
    openpet_core_set_log_callback(m_core, nullptr, nullptr);
    openpet_core_free(m_core);
    m_core = nullptr;
}

void CoreBridge::onReaction(const OpenPetReaction *reaction, void *userData)
{
    auto *self = static_cast<CoreBridge *>(userData);
    if (!self || !reaction)
        return;

    const Reaction converted = fromFfi(*reaction);

    // Callback может прийти из потока ядра. Очередь Qt переносит реакцию
    // в поток, которому принадлежит CoreBridge.
    QMetaObject::invokeMethod(
        self,
        [self, converted] { emit self->reactionReceived(converted); },
        Qt::QueuedConnection);
}

void CoreBridge::onLog(qint32 level, const char *message, void *userData)
{
    auto *self = static_cast<CoreBridge *>(userData);
    if (!self || !message)
        return;

    const QString text = QString::fromUtf8(message);
    QMetaObject::invokeMethod(
        self,
        [self, level, text] { emit self->diagnostic(level, text); },
        Qt::QueuedConnection);
}

bool CoreBridge::push(OpenPetEvent &event)
{
    if (!m_core)
        return false;

    OpenPetReaction raw {};
    const qint32 result = openpet_core_push_event(m_core, &event, &raw);

    if (result < 0) {
        qCWarning(logCore, "ядро отклонило событие вида %u с кодом %d", event.kind, result);
        return false;
    }

    if (result == 0)
        return false;

    emit reactionReceived(fromFfi(raw));
    return true;
}

bool CoreBridge::pushActivityResumed()
{
    OpenPetEvent event = makeEvent(OPENPET_EVENT_ACTIVITY_RESUMED);
    return push(event);
}

bool CoreBridge::pushIdleThreshold(quint32 seconds)
{
    OpenPetEvent event = makeEvent(OPENPET_EVENT_IDLE_THRESHOLD_REACHED);
    event.idle_seconds = seconds;
    return push(event);
}

bool CoreBridge::pushPowerChanged(bool onBattery, int percent, OpenPetPowerState state)
{
    OpenPetEvent event = makeEvent(OPENPET_EVENT_POWER_CHANGED);
    event.on_battery = onBattery ? 1 : 0;
    event.power_state = state;
    if (percent >= 0 && percent <= 100) {
        event.battery_percent_valid = 1;
        event.battery_percent = uint8_t(percent);
    }
    return push(event);
}

bool CoreBridge::pushSessionChanged(OpenPetSessionState state)
{
    OpenPetEvent event = makeEvent(OPENPET_EVENT_SESSION_CHANGED);
    event.session_state = state;
    return push(event);
}

bool CoreBridge::pushActiveAppChanged(const QString &appId)
{
    // Буфер обязан пережить вызов — отсюда именованная переменная,
    // а не временная в списке аргументов.
    const QByteArray utf8 = appId.toUtf8();

    OpenPetEvent event = makeEvent(OPENPET_EVENT_ACTIVE_APP_CHANGED);
    event.app_id = utf8.isEmpty() ? nullptr : utf8.constData();
    event.app_id_len = size_t(utf8.size());
    return push(event);
}

bool CoreBridge::pushNotification(const QString &category)
{
    const QByteArray utf8 = category.toUtf8();

    OpenPetEvent event = makeEvent(OPENPET_EVENT_NOTIFICATION_OCCURRED);
    event.category = utf8.isEmpty() ? nullptr : utf8.constData();
    event.category_len = size_t(utf8.size());
    return push(event);
}

bool CoreBridge::pushMediaChanged(OpenPetMediaState state)
{
    OpenPetEvent event = makeEvent(OPENPET_EVENT_MEDIA_CHANGED);
    event.media_state = state;
    return push(event);
}

bool CoreBridge::pushPetClicked()
{
    OpenPetEvent event = makeEvent(OPENPET_EVENT_PET_CLICKED);
    return push(event);
}

bool CoreBridge::pushPetDragged()
{
    OpenPetEvent event = makeEvent(OPENPET_EVENT_PET_DRAGGED);
    return push(event);
}

void CoreBridge::setPaused(bool paused)
{
    if (m_core)
        openpet_core_set_paused(m_core, paused ? 1 : 0);
}

bool CoreBridge::isPaused() const
{
    return m_core && openpet_core_is_paused(m_core) != 0;
}

int CoreBridge::currentEmotion() const
{
    return m_core ? int(openpet_core_current_emotion(m_core)) : 0;
}

void CoreBridge::setLowBatteryThreshold(int percent)
{
    if (m_core && percent >= 0 && percent <= 100)
        openpet_core_set_low_battery_threshold(m_core, uint8_t(percent));
}

CoreBridge::Animation CoreBridge::animationFor(const QString &state) const
{
    Animation animation;
    if (!m_core)
        return animation;

    const QByteArray utf8 = state.toUtf8();
    OpenPetAnimation raw {};
    openpet_core_animation(m_core, utf8.constData(), size_t(utf8.size()), &raw);

    if (raw.frames == 0)
        return animation;

    animation.row = int(raw.row);
    animation.startColumn = int(raw.start_column);
    animation.frames = int(raw.frames);
    animation.frameDurationMs = int(raw.frame_duration_ms);
    animation.cellWidth = int(raw.cell_width);
    animation.cellHeight = int(raw.cell_height);
    return animation;
}

void CoreBridge::setLlmProvider(int kind, const QString &baseUrl, const QString &model,
                                const QString &project, const QString &region)
{
    if (!m_core)
        return;

    // Буферы обязаны пережить вызов — отсюда именованные переменные.
    const QByteArray url = baseUrl.toUtf8();
    const QByteArray modelName = model.toUtf8();
    const QByteArray projectId = project.toUtf8();
    const QByteArray regionName = region.toUtf8();

    OpenPetLlmConfig config {};
    config.kind = uint32_t(kind);
    config.base_url = url.constData();
    config.base_url_len = size_t(url.size());
    config.model = modelName.constData();
    config.model_len = size_t(modelName.size());
    config.project = projectId.constData();
    config.project_len = size_t(projectId.size());
    config.region = regionName.constData();
    config.region_len = size_t(regionName.size());

    openpet_core_set_llm(m_core, &config);
}

bool CoreBridge::isLlmEnabled() const
{
    return m_core && openpet_core_llm_enabled(m_core) != 0;
}

bool CoreBridge::buildLlmRequest(LlmRequest *out) const
{
    if (!m_core || !out)
        return false;

    OpenPetLlmRequest raw {};
    if (openpet_core_build_llm_request(m_core, &raw) != 1)
        return false;

    out->url = QString::fromUtf8(raw.url);
    out->body = QString::fromUtf8(raw.body);
    out->timeoutMs = int(raw.timeout_ms);
    return true;
}

CoreBridge::PackInstall CoreBridge::installPack(const QByteArray &archive, QString *outSheetPath)
{
    PackInstall result;
    if (!m_core)
        return result;

    QByteArray report(OPENPET_REPORT_SIZE, '\0');
    const int verdict = openpet_core_install_pack(m_core, archive.constData(),
                                                  size_t(archive.size()), report.data(),
                                                  size_t(report.size()));
    result.report = QString::fromUtf8(report.constData());
    result.accepted = verdict == 1;

    if (!result.accepted)
        return result;

    // Лист забирается у ядра один раз: там он не хранится, чтобы не висеть
    // в памяти дважды — в ядре и в текстуре.
    const size_t size = openpet_core_pending_sheet_size(m_core);
    if (size == 0)
        return result;

    QByteArray sheet(int(size), '\0');
    const int written = openpet_core_take_sheet(m_core, sheet.data(), size);
    if (written <= 0) {
        result.accepted = false;
        result.report = tr("не удалось получить лист из пакета");
        return result;
    }
    sheet.truncate(written);

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/packs/") + activePackId();
    if (!QDir().mkpath(dir)) {
        result.accepted = false;
        result.report = tr("не удалось создать каталог для пакета");
        return result;
    }

    // Архив кладётся рядом: при следующем запуске пакет ставится заново
    // и заново проходит проверку. Хранить только распакованный лист значило
    // бы доверять собственному кэшу больше, чем валидатору.
    QFile archiveCopy(dir + QStringLiteral("/pack.zip"));
    if (archiveCopy.open(QIODevice::WriteOnly | QIODevice::Truncate))
        archiveCopy.write(archive);

    const QString path = dir + QStringLiteral("/sheet.png");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(sheet) != sheet.size()) {
        // Записать наполовину хуже, чем не записать: питомец покажет мусор.
        file.remove();
        result.accepted = false;
        result.report = tr("не удалось записать лист на диск");
        return result;
    }
    file.close();

    if (outSheetPath)
        *outSheetPath = path;

    return result;
}

void CoreBridge::rollbackPack()
{
    if (m_core)
        openpet_core_rollback_pack(m_core);
}

QString CoreBridge::activePackId() const
{
    if (!m_core)
        return {};

    QByteArray id(128, '\0');
    QByteArray sheet(256, '\0');
    openpet_core_active_pack(m_core, id.data(), size_t(id.size()), sheet.data(),
                             size_t(sheet.size()));
    return QString::fromUtf8(id.constData());
}

CoreBridge::TokenExchange CoreBridge::buildTokenRequest(const QByteArray &adc) const
{
    TokenExchange result;
    if (!m_core)
        return result;

    OpenPetLlmRequest plan {};
    const int verdict = openpet_core_build_token_request(m_core, adc.constData(),
                                                         size_t(adc.size()), &plan);
    if (verdict == -3) {
        result.serviceAccountUnsupported = true;
        return result;
    }
    if (verdict != 1)
        return result;

    result.request.url = QString::fromUtf8(plan.url);
    result.request.body = QString::fromUtf8(plan.body);
    result.request.timeoutMs = int(plan.timeout_ms);
    result.ok = true;
    return result;
}

int CoreBridge::acceptTokenResponse(const QByteArray &raw, QString *outToken) const
{
    if (!m_core || !outToken)
        return 0;

    QByteArray token(OPENPET_TOKEN_SIZE, '\0');
    uint32_t expires = 0;
    const int verdict = openpet_core_accept_token_response(m_core, raw.constData(),
                                                           size_t(raw.size()), token.data(),
                                                           size_t(token.size()), &expires);
    if (verdict != 1)
        return 0;

    *outToken = QString::fromUtf8(token.constData());
    return int(expires);
}

bool CoreBridge::buildHealthRequest(LlmRequest *out) const
{
    if (!m_core || !out)
        return false;

    OpenPetLlmRequest plan {};
    if (openpet_core_build_health_request(m_core, &plan) != 1)
        return false;

    out->url = QString::fromUtf8(plan.url);
    out->body.clear();
    out->timeoutMs = int(plan.timeout_ms);
    return true;
}

QStringList CoreBridge::acceptModelList(const QByteArray &raw) const
{
    if (!m_core)
        return {};

    QByteArray names(OPENPET_MODEL_LIST_SIZE, '\0');
    if (openpet_core_accept_model_list(m_core, raw.constData(), size_t(raw.size()), names.data(),
                                       size_t(names.size()))
        < 0)
        return {};

    return QString::fromUtf8(names.constData())
        .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

int CoreBridge::acceptHealthResponse(const QByteArray &raw) const
{
    if (!m_core)
        return -1;
    return openpet_core_accept_health_response(m_core, raw.constData(), size_t(raw.size()));
}

QString CoreBridge::acceptLlmResponse(const QByteArray &raw) const
{
    if (!m_core)
        return {};

    // Тот же предел, что и у реплики: длиннее ядро всё равно не отдаст.
    char buffer[OPENPET_PHRASE_SIZE] {};
    const qint32 result = openpet_core_accept_llm_response(
        m_core, raw.constData(), size_t(raw.size()), buffer, sizeof(buffer));

    return result == 1 ? QString::fromUtf8(buffer) : QString();
}

void CoreBridge::setLocale(const QString &tag)
{
    if (!m_core)
        return;

    const QByteArray utf8 = tag.toUtf8();
    openpet_core_set_locale(m_core, utf8.constData(), size_t(utf8.size()));
}

void CoreBridge::clearPhraseHistory()
{
    if (m_core)
        openpet_core_clear_phrase_history(m_core);
}

void CoreBridge::settle()
{
    if (!m_core)
        return;

    uint32_t emotion = 0;
    if (openpet_core_settle(m_core, &emotion) == 1)
        emit settled(int(emotion));

    // Микродвижение проверяется тем же тиком: заводить второй таймер ради
    // события раз в полминуты значит будить процесс вдвое чаще без нужды.
    OpenPetReaction raw {};
    if (openpet_core_fidget(m_core, &raw) == 1)
        emit fidgeted(int(raw.animation));
}
