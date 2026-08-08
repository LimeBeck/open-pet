#pragma once

#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QDateTime>
#include <QUrl>

#include <functional>
#include <QString>

class QNetworkReply;
class CoreBridge;

// Транспорт LLM-шлюза ([ADR-008](../../../docs/adr/0008-llm-transport-boundary.md)).
//
// Здесь только сеть, секрет и таймаут. Что уходит в теле запроса, решает
// ядро; добавлять к телу что-либо запрещено. Заголовок с ключом добавляется
// здесь и только здесь — до ядра ключ не доходит.
class LlmClient : public QObject
{
    Q_OBJECT

public:
    explicit LlmClient(CoreBridge *core, QObject *parent = nullptr);

    bool isEnabled() const;

    // Ключ живёт только в памяти этого объекта и в Secret Service.
    // В настройки и в журнал он не попадает никогда (§FR-7).
    void setApiKey(const QString &key);

    // Прокси: 0 — системный, 1 — без прокси, 2 — заданный вручную.
    void setProxy(int mode, const QString &host, int port, const QString &user,
                  const QString &password, bool bypassLocal);

    // Запрашивает реплику для последней реакции. Если LLM выключена или
    // ядру нечего спросить, сигнал не придёт вовсе.
    void requestPhrase();

    // Проверка связи из окна настроек. Результат приходит сигналом:
    // ждать ответа сети синхронно — значит подвесить интерфейс.
    void checkHealth();

    // Vertex AI не принимает статический ключ: нужен токен доступа,
    // полученный обменом учётных данных Google ADC. Токен живёт около часа,
    // поэтому обновляется по надобности, а не однажды при запуске.
    void setVertexCredentialsPath(const QString &path);
    // Вид провайдера нужен, чтобы знать, требуется ли токен: 3 — Vertex AI.
    void setProviderKind(int kind) { m_providerKind = kind; }

signals:
    // Годная реплика от модели.
    void phraseReady(const QString &phrase);
    // Ответа не будет: ошибка, таймаут, offline или негодный ответ.
    // Хост показывает локальный шаблон (§FR-6).
    void phraseFailed(const QString &reason);

    // ok — дозвонились; modelFound — настроенная модель у провайдера есть.
    // Это разные вещи: без разделения пользователь чинил бы сеть вместо
    // опечатки в названии модели.
    void healthChecked(bool ok, bool modelFound, const QString &detail);
    // Список моделей провайдера. Приходит вместе с проверкой связи:
    // отдельный запрос за тем же самым был бы лишним обращением к сети.
    void modelsListed(const QStringList &models);

private:
    void applyProxy(const QUrl &url);
    // Возвращает false, если токена нет и получить его сейчас нельзя.
    // Тогда запрос не отправляется вовсе, а питомец берёт шаблон (§FR-6).
    bool ensureAccessToken(const std::function<void(bool)> &done);
    // Требует ли провайдер токена, полученного обменом.
    bool needsToken() const { return m_providerKind == 3; }
    void sendPhraseRequest();
    void sendHealthRequest();
    QString authorizationValue() const;
    void applyAuthorization(QNetworkRequest &request) const;
    void finish(QNetworkReply *reply);

    CoreBridge *m_core = nullptr;
    QNetworkAccessManager m_network;
    QString m_apiKey;
    int m_proxyMode = 0;
    QNetworkProxy m_manualProxy;
    bool m_proxyBypassLocal = true;
    QPointer<QNetworkReply> m_inFlight;
    QPointer<QNetworkReply> m_healthInFlight;

    int m_providerKind = 0;
    QString m_adcPath;
    QString m_accessToken;
    QDateTime m_tokenExpiry;
};
