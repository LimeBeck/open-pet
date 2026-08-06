#pragma once

#include "openpet_core.h"

#include <QObject>
#include <QString>

// Безопасная обёртка над C ABI ядра.
//
// Отвечает ровно за три вещи: владение указателем, перевод Qt-типов в POD
// и перекладывание реакций из потока ядра в поток Qt. Домена здесь нет —
// он весь на стороне Rust.
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
    };

    explicit CoreBridge(QObject *parent = nullptr);
    ~CoreBridge() override;

    CoreBridge(const CoreBridge &) = delete;
    CoreBridge &operator=(const CoreBridge &) = delete;

    bool isValid() const { return m_core != nullptr; }

    // Синхронный путь: событие → реакция. Возвращает false, если ядро
    // подавило событие по cooldown или приоритету.
    bool pushActivityResumed(Reaction *out);
    bool pushIdleThreshold(quint32 seconds, Reaction *out);
    bool pushPowerChanged(bool onBattery, int percent, Reaction *out);
    bool pushActiveAppChanged(const QString &appId, Reaction *out);
    bool pushPetClicked(Reaction *out);

    // Асинхронный путь: ядро само присылает реакции из своего потока.
    void startTicker(quint32 intervalMs);

    // Проверка, что паника в Rust не уносит с собой процесс хоста.
    int simulatePanic();

signals:
    // Всегда испускается в потоке, которому принадлежит CoreBridge.
    void reactionReceived(const CoreBridge::Reaction &reaction);

private:
    static void onReaction(const OpenPetReaction *reaction, void *userData);
    void deliver(const Reaction &reaction);

    OpenPetCore *m_core = nullptr;
};

Q_DECLARE_METATYPE(CoreBridge::Reaction)
