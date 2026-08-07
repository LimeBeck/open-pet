#include "llmclient.h"

#include "corebridge.h"

#include <QLoggingCategory>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
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

void LlmClient::applyProxy(const QUrl &url)
{
    // Локальный провайдер не ходит через прокси, пока пользователь явно
    // не потребовал обратного. Ollama на 127.0.0.1 — основной сценарий
    // приватного режима, и заворачивать её во внешний прокси значит
    // отправлять наружу то, что должно остаться дома.
    const QString host = url.host();
    const bool isLocal = host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost")
        || host == QLatin1String("::1") || host.endsWith(QLatin1String(".localhost"));

    if (isLocal && m_proxyBypassLocal) {
        m_network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        return;
    }

    switch (m_proxyMode) {
    case 1:
        m_network.setProxy(QNetworkProxy(QNetworkProxy::NoProxy));
        break;
    case 2:
        m_network.setProxy(m_manualProxy);
        break;
    default:
        // Системный: сбрасываем свой прокси, дальше решает фабрика Qt.
        m_network.setProxy(QNetworkProxy(QNetworkProxy::DefaultProxy));
        break;
    }
}

void LlmClient::setProxy(int mode, const QString &host, int port, const QString &user,
                         const QString &password, bool bypassLocal)
{
    m_proxyMode = mode;
    m_proxyBypassLocal = bypassLocal;

    m_manualProxy = QNetworkProxy(QNetworkProxy::HttpProxy, host, quint16(port), user, password);

    if (mode == 0) {
        // Системная настройка. Прокси берётся из окружения и настроек KDE —
        // приложение не изобретает свою политику там, где она уже задана.
        QNetworkProxyFactory::setUseSystemConfiguration(true);
    }
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

    applyProxy(url);
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

void LlmClient::checkHealth()
{
    if (!isEnabled()) {
        emit healthChecked(false, false, tr("провайдер не выбран"));
        return;
    }

    CoreBridge::LlmRequest plan;
    if (!m_core->buildHealthRequest(&plan)) {
        emit healthChecked(false, false, tr("ядро не смогло собрать запрос"));
        return;
    }

    if (m_healthInFlight) {
        m_healthInFlight->abort();
        m_healthInFlight->deleteLater();
        m_healthInFlight = nullptr;
    }

    const QUrl url(plan.url);
    QNetworkRequest request { url };
    request.setTransferTimeout(plan.timeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    applyProxy(url);

    if (!m_apiKey.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());

    // Проверка связи — обычный GET: спрашивается список моделей, а не
    // генерация. Проверять связь генерацией значит платить за проверку
    // временем модели и получать «медленно» вместо «недоступно».
    QNetworkReply *reply = m_network.get(request);
    m_healthInFlight = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (m_healthInFlight == reply)
            m_healthInFlight = nullptr;
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(logLlm) << "проверка связи не удалась, код" << int(reply->error());
            emit healthChecked(false, false,
                               tr("нет ответа (код %1)").arg(int(reply->error())));
            return;
        }

        const int verdict = m_core->acceptHealthResponse(reply->readAll());
        if (verdict < 0) {
            emit healthChecked(false, false, tr("ответ не разобран"));
            return;
        }

        emit healthChecked(true, verdict == 1,
                           verdict == 1 ? tr("провайдер доступен, модель найдена")
                                        : tr("провайдер доступен, но модели нет"));
    });
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
