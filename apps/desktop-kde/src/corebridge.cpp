#include "corebridge.h"

#include <QByteArray>
#include <QLoggingCategory>
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

void CoreBridge::settle()
{
    if (!m_core)
        return;

    uint32_t emotion = 0;
    if (openpet_core_settle(m_core, &emotion) == 1)
        emit settled(int(emotion));
}
