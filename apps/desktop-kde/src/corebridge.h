#pragma once

#include "openpet_core.h"

#include <QObject>
#include <QString>

// Безопасная обёртка над C ABI ядра.
//
// Отвечает ровно за три вещи: владение указателем, перевод Qt-типов в POD
// и перенос реакций из потока ядра в поток Qt. Логики поведения здесь нет —
// она вся в Rust (ADR-001).
class CoreBridge : public QObject
{
    Q_OBJECT

public:
    struct Reaction {
        int emotion = 0;
        int animation = 0;
        int priority = 0;
        int ttlMs = 0;
        QString cooldownKey;
        // Пустая строка означает «питомец молчит»: не каждая смена позы
        // заслуживает слов (§FR-6).
        QString phrase;
    };

    explicit CoreBridge(QObject *parent = nullptr);
    ~CoreBridge() override;

    CoreBridge(const CoreBridge &) = delete;
    CoreBridge &operator=(const CoreBridge &) = delete;

    bool isValid() const { return m_core != nullptr; }

    // Источники событий (§FR-4). Каждый возвращает true, если ядро
    // сформировало реакцию, и false, если событие подавлено.
    bool pushActivityResumed();
    bool pushIdleThreshold(quint32 seconds);
    bool pushPowerChanged(bool onBattery, int percent, OpenPetPowerState state);
    bool pushSessionChanged(OpenPetSessionState state);
    bool pushActiveAppChanged(const QString &appId);
    bool pushNotification(const QString &category);
    bool pushMediaChanged(OpenPetMediaState state);
    bool pushPetClicked();

    void setPaused(bool paused);
    bool isPaused() const;

    int currentEmotion() const;
    void setLowBatteryThreshold(int percent);

    // Локаль реплик. Неизвестные теги ядро приводит к английскому (§7).
    void setLocale(const QString &tag);
    void clearPhraseHistory();

    // Возврат в покой по истечении ttl. Вызывается по таймеру хоста.
    void settle();

signals:
    void reactionReceived(const CoreBridge::Reaction &reaction);
    // Ядро вернулось в покой само, без внешнего события.
    void settled(int emotion);
    void diagnostic(int level, const QString &message);

private:
    static void onReaction(const OpenPetReaction *reaction, void *userData);
    static void onLog(qint32 level, const char *message, void *userData);

    bool push(OpenPetEvent &event);

    OpenPetCore *m_core = nullptr;
};

Q_DECLARE_METATYPE(CoreBridge::Reaction)
