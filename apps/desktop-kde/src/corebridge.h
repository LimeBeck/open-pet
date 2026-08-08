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
    // Перетаскивание — отдельное обращение, а не разновидность клика.
    bool pushPetDragged();

    void setPaused(bool paused);
    bool isPaused() const;

    int currentEmotion() const;
    void setLowBatteryThreshold(int percent);

    // Раскладка кадров для состояния. UI не хранит собственную таблицу:
    // где какая анимация лежит, знает только ядро (ADR-005).
    struct Animation {
        int row = 0;
        int startColumn = 0;
        int frames = 1;
        int frameDurationMs = 200;
        int cellWidth = 0;
        int cellHeight = 0;

        // Сравнение нужно, чтобы не дёргать UI, когда раскладка та же:
        // микродвижение может указать на анимацию, уже показываемую.
        bool operator==(const Animation &other) const = default;
    };
    Animation animationFor(const QString &state) const;

    // LLM (§FR-7). Ключ сюда не передаётся: он живёт только в хосте,
    // а ядро формирует тело запроса без него (ADR-008).
    struct LlmRequest {
        QString url;
        QString body;
        int timeoutMs = 2500;
    };

    void setLlmProvider(int kind, const QString &baseUrl, const QString &model,
                        const QString &project = {}, const QString &region = {});
    bool isLlmEnabled() const;
    bool buildLlmRequest(LlmRequest *out) const;
    // Пустая строка означает «ответ негоден» — показывается шаблон (§FR-6).
    QString acceptLlmResponse(const QByteArray &raw) const;

    struct PackInstall {
        bool accepted = false;
        // Замечания приходят и при успехе: у принятого пакета бывают
        // предупреждения, и молчать о них — значит скрывать полдиагностики.
        QString report;
    };

    // Устанавливает Pet Pack и, при успехе, записывает лист в каталог данных.
    // Путь к листу возвращается через outSheetPath.
    PackInstall installPack(const QByteArray &archive, QString *outSheetPath);
    void rollbackPack();
    QString activePackId() const;

    struct TokenExchange {
        bool ok = false;
        // Отдельно от общей неудачи: чинить нужно способ входа, а не файл.
        bool serviceAccountUnsupported = false;
        LlmRequest request;
    };

    // Обмен учётных данных Google ADC на токен доступа (ADR-008, «Уточнение»).
    TokenExchange buildTokenRequest(const QByteArray &adc) const;
    // Возвращает срок жизни в секундах, 0 при неудаче.
    int acceptTokenResponse(const QByteArray &raw, QString *outToken) const;

    // Проверка связи (§FR-7). Возвращает false, если ядру нечего спросить.
    bool buildHealthRequest(LlmRequest *out) const;
    // 1 — провайдер ответил и модель есть, 0 — модели нет, <0 — не разобрано.
    int acceptHealthResponse(const QByteArray &raw) const;
    // Список моделей из того же ответа — для выпадающего списка в настройках.
    QStringList acceptModelList(const QByteArray &raw) const;

    // Локаль реплик. Неизвестные теги ядро приводит к английскому (§7).
    void setLocale(const QString &tag);
    void clearPhraseHistory();

    // Возврат в покой по истечении ttl. Вызывается по таймеру хоста.
    void settle();

signals:
    void reactionReceived(const CoreBridge::Reaction &reaction);
    // Ядро вернулось в покой само, без внешнего события.
    void settled(int emotion);
    // Питомец шевельнулся в покое: меняется только анимация, не эмоция.
    void fidgeted(int animation);
    void diagnostic(int level, const QString &message);

private:
    static void onReaction(const OpenPetReaction *reaction, void *userData);
    static void onLog(qint32 level, const char *message, void *userData);

    bool push(OpenPetEvent &event);

    OpenPetCore *m_core = nullptr;
};

Q_DECLARE_METATYPE(CoreBridge::Reaction)
