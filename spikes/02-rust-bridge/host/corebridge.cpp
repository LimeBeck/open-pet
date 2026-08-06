#include "corebridge.h"

#include <QByteArray>
#include <QDebug>
#include <QMetaObject>

namespace {

OpenPetEvent makeEvent(OpenPetEventKind kind)
{
    // Инициализация нулями обязательна: структура плоская, и поля, не
    // относящиеся к текущему kind, ядро всё равно прочитает.
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
        // Ядро и хост собраны из разных версий контракта. Продолжать нельзя:
        // раскладка структур могла измениться, и чтение полей даст мусор.
        qCritical("Несовпадение ABI ядра: хост ждёт %d, ядро сообщает %u",
                  OPENPET_ABI_VERSION, openpet_abi_version());
        return;
    }

    m_core = openpet_core_new();
    if (!m_core) {
        qCritical("Не удалось создать ядро");
        return;
    }

    openpet_core_set_callback(m_core, &CoreBridge::onReaction, this);
}

CoreBridge::~CoreBridge()
{
    if (!m_core)
        return;

    // Сначала снимаем callback, потом освобождаем ядро: иначе поток тикера
    // успеет позвать нас уже после начала разрушения объекта.
    openpet_core_set_callback(m_core, nullptr, nullptr);
    openpet_core_free(m_core);
    m_core = nullptr;
}

void CoreBridge::onReaction(const OpenPetReaction *reaction, void *userData)
{
    auto *self = static_cast<CoreBridge *>(userData);
    if (!self || !reaction)
        return;

    self->deliver(fromFfi(*reaction));
}

void CoreBridge::deliver(const Reaction &reaction)
{
    // Callback приходит из потока ядра. Очередь Qt переносит реакцию
    // в поток, которому принадлежит CoreBridge, — это и есть вся интеграция
    // асинхронного ядра с event loop.
    QMetaObject::invokeMethod(
        this,
        [this, reaction] { emit reactionReceived(reaction); },
        Qt::QueuedConnection);
}

bool CoreBridge::pushActivityResumed(Reaction *out)
{
    if (!m_core)
        return false;

    OpenPetEvent event = makeEvent(OPENPET_EVENT_ACTIVITY_RESUMED);
    OpenPetReaction raw {};
    if (openpet_core_push_event(m_core, &event, &raw) != 1)
        return false;

    if (out)
        *out = fromFfi(raw);
    return true;
}

bool CoreBridge::pushIdleThreshold(quint32 seconds, Reaction *out)
{
    if (!m_core)
        return false;

    OpenPetEvent event = makeEvent(OPENPET_EVENT_IDLE_THRESHOLD_REACHED);
    event.idle_seconds = seconds;

    OpenPetReaction raw {};
    if (openpet_core_push_event(m_core, &event, &raw) != 1)
        return false;

    if (out)
        *out = fromFfi(raw);
    return true;
}

bool CoreBridge::pushPowerChanged(bool onBattery, int percent, Reaction *out)
{
    if (!m_core)
        return false;

    OpenPetEvent event = makeEvent(OPENPET_EVENT_POWER_CHANGED);
    event.on_battery = onBattery ? 1 : 0;
    if (percent >= 0 && percent <= 100) {
        event.battery_percent_valid = 1;
        event.battery_percent = uint8_t(percent);
    }

    OpenPetReaction raw {};
    if (openpet_core_push_event(m_core, &event, &raw) != 1)
        return false;

    if (out)
        *out = fromFfi(raw);
    return true;
}

bool CoreBridge::pushActiveAppChanged(const QString &appId, Reaction *out)
{
    if (!m_core)
        return false;

    // Буфер обязан пережить вызов — отсюда именованная переменная,
    // а не временная в списке аргументов.
    const QByteArray utf8 = appId.toUtf8();

    OpenPetEvent event = makeEvent(OPENPET_EVENT_ACTIVE_APP_CHANGED);
    event.app_id = utf8.isEmpty() ? nullptr : utf8.constData();
    event.app_id_len = size_t(utf8.size());

    OpenPetReaction raw {};
    if (openpet_core_push_event(m_core, &event, &raw) != 1)
        return false;

    if (out)
        *out = fromFfi(raw);
    return true;
}

bool CoreBridge::pushPetClicked(Reaction *out)
{
    if (!m_core)
        return false;

    OpenPetEvent event = makeEvent(OPENPET_EVENT_PET_CLICKED);
    OpenPetReaction raw {};
    if (openpet_core_push_event(m_core, &event, &raw) != 1)
        return false;

    if (out)
        *out = fromFfi(raw);
    return true;
}

void CoreBridge::startTicker(quint32 intervalMs)
{
    if (m_core)
        openpet_core_start_ticker(m_core, intervalMs);
}

int CoreBridge::simulatePanic()
{
    return m_core ? openpet_core_simulate_panic(m_core) : 0;
}
