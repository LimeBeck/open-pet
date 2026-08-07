#include "llmclient.h"

#include "corebridge.h"

#include <QLoggingCategory>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

Q_LOGGING_CATEGORY(logLlm, "openpet.llm")

LlmClient::LlmClient(CoreBridge *core, QObject *parent)
    : QObject(parent)
    , m_core(core)
{
    // Пока провайдер не настроен, сетевого стека мы даже не трогаем:
    // §7 требует ноль запросов, а не «ноль полезных запросов».
    m_network.setAutoDeleteReplies(false);
}

bool LlmClient::isEnabled() const
{
    return m_core && m_core->isLlmEnabled();
}

void LlmClient::setApiKey(const QString &key)
{
    m_apiKey = key;
}

void LlmClient::requestPhrase()
{
    // Каждый выход отсюда обязан сообщить о неудаче. Молчаливый возврат
    // оставляет придержанный шаблон ждать ответа, которого не будет,
    // и питомец немеет навсегда — хуже, чем неработающая LLM.
    if (!isEnabled()) {
        emit phraseFailed(QStringLiteral("provider-disabled"));
        return;
    }

    CoreBridge::LlmRequest plan;
    if (!m_core->buildLlmRequest(&plan)) {
        qCWarning(logLlm, "ядро не дало запроса, показываем шаблон");
        emit phraseFailed(QStringLiteral("no-request"));
        return;
    }

    qCDebug(logLlm) << "запрос к" << plan.url << "тело" << plan.body.size() << "байт";

    // Один запрос за раз: питомец не ведёт диалог, ему нужна одна фраза
    // на одно событие. Предыдущий ответ уже неактуален.
    if (m_inFlight) {
        m_inFlight->abort();
        m_inFlight->deleteLater();
        m_inFlight = nullptr;
    }

    const QUrl url(plan.url);

    QNetworkRequest request { url };

    // Локальный провайдер не ходит через прокси. Ollama на 127.0.0.1 —
    // основной сценарий приватного режима, и заворачивать её во внешний
    // прокси значит отправлять наружу то, что должно остаться дома.
    const QString host = url.host();
    if (host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost")
        || host == QLatin1String("::1")) {
        request.setAttribute(QNetworkRequest::UseCredentialsAttribute, false);
        m_network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // Таймаут считает хост (ADR-008). Значение ядра — разумное умолчание,
    // но реальные локальные модели в него не укладываются: qwen3.5:4b даёт
    // ответ за 9 секунд даже с ограниченной генерацией. Пока настроек нет,
    // переопределяется окружением.
    const int timeout = qEnvironmentVariableIntValue("OPENPET_LLM_TIMEOUT_MS") > 0
        ? qEnvironmentVariableIntValue("OPENPET_LLM_TIMEOUT_MS")
        : plan.timeoutMs;
    request.setTransferTimeout(timeout);
    // Перенаправления запрещены: провайдер, уводящий запрос на другой хост,
    // уводит туда и ключ.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);

    if (!m_apiKey.isEmpty()) {
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
    }

    QNetworkReply *reply = m_network.post(request, plan.body.toUtf8());
    m_inFlight = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply] { finish(reply); });
}

void LlmClient::finish(QNetworkReply *reply)
{
    if (m_inFlight == reply)
        m_inFlight = nullptr;

    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        // Текст ошибки Qt не содержит ключа, но может содержать URL.
        // В журнал уходит только вид ошибки (§9).
        const QString reason = QString::number(int(reply->error()));
        qCWarning(logLlm) << "запрос не удался, код" << reason << "— показываем шаблон";
        emit phraseFailed(reason);
        return;
    }

    const QByteArray raw = reply->readAll();

    // Разбор ответа — в ядре: это недоверенный JSON, и провайдер мог
    // оказаться не тем, за кого себя выдаёт (ADR-007).
    const QString phrase = m_core->acceptLlmResponse(raw);
    if (phrase.isEmpty()) {
        qCWarning(logLlm, "ответ негоден, показываем шаблон");
        emit phraseFailed(QStringLiteral("negodny"));
        return;
    }

    emit phraseReady(phrase);
}
