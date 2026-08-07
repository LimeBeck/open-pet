#pragma once

#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QObject>
#include <QPointer>
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

signals:
    // Годная реплика от модели.
    void phraseReady(const QString &phrase);
    // Ответа не будет: ошибка, таймаут, offline или негодный ответ.
    // Хост показывает локальный шаблон (§FR-6).
    void phraseFailed(const QString &reason);

private:
    void finish(QNetworkReply *reply);

    CoreBridge *m_core = nullptr;
    QNetworkAccessManager m_network;
    QString m_apiKey;
    int m_proxyMode = 0;
    QNetworkProxy m_manualProxy;
    bool m_proxyBypassLocal = true;
    QPointer<QNetworkReply> m_inFlight;
};
