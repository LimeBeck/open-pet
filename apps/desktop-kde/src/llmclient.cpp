#include "llmclient.h"

#include "corebridge.h"

#include <QLoggingCategory>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFile>
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
    if (needsToken()) {
        // Токен добывается до запроса, а не по 401: получить отказ уже
        // на реплике значит промолчать там, где шаблон был бы уместнее.
        ensureAccessToken([this](bool ok) {
            if (ok)
                sendPhraseRequest();
            else
                emit phraseFailed(QStringLiteral("no-token"));
        });
        return;
    }

    sendPhraseRequest();
}

void LlmClient::sendPhraseRequest()
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

    const QString authorization = authorizationValue();
    if (!authorization.isEmpty())
        request.setRawHeader("Authorization", authorization.toUtf8());

    QNetworkReply *reply = m_network.post(request, plan.body.toUtf8());
    m_inFlight = reply;

    connect(reply, &QNetworkReply::finished, this, [this, reply] { finish(reply); });
}

void LlmClient::setVertexCredentialsPath(const QString &path)
{
    if (m_adcPath == path)
        return;

    m_adcPath = path;
    // Учётные данные сменились — прежний токен к ним отношения не имеет.
    m_accessToken.clear();
    m_tokenExpiry = QDateTime();
}

QString LlmClient::authorizationValue() const
{
    // Для Vertex — токен ADC, для остальных — ключ пользователя.
    if (!m_accessToken.isEmpty())
        return QStringLiteral("Bearer ") + m_accessToken;
    if (!m_apiKey.isEmpty())
        return QStringLiteral("Bearer ") + m_apiKey;
    return {};
}

bool LlmClient::ensureAccessToken(const std::function<void(bool)> &done)
{
    // Токен обновляется заранее: получить 401 на реплике питомца значит
    // промолчать там, где шаблон был бы уместнее.
    const bool valid = !m_accessToken.isEmpty() && m_tokenExpiry.isValid()
        && QDateTime::currentDateTimeUtc().secsTo(m_tokenExpiry) > 60;
    if (valid) {
        done(true);
        return true;
    }

    if (m_adcPath.isEmpty()) {
        qCWarning(logLlm, "путь к учётным данным Google не задан");
        done(false);
        return false;
    }

    QFile file(m_adcPath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(logLlm, "учётные данные Google не читаются");
        done(false);
        return false;
    }

    const QByteArray adc = file.readAll();
    file.close();

    const CoreBridge::TokenExchange exchange = m_core->buildTokenRequest(adc);
    if (!exchange.ok) {
        if (exchange.serviceAccountUnsupported)
            qCWarning(logLlm, "учётные данные сервисного аккаунта не поддерживаются");
        else
            qCWarning(logLlm, "учётные данные Google не разобраны");
        done(false);
        return false;
    }

    QNetworkRequest request { QUrl(exchange.request.url) };
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(exchange.request.timeoutMs);
    applyProxy(QUrl(exchange.request.url));

    QNetworkReply *reply = m_network.post(request, exchange.request.body.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply, done] {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            // Тело ответа не логируется: в нём бывает описание ошибки
            // вместе с фрагментами учётных данных.
            qCWarning(logLlm) << "обмен токена не удался, код" << int(reply->error());
            done(false);
            return;
        }

        QString token;
        const int lifetime = m_core->acceptTokenResponse(reply->readAll(), &token);
        if (lifetime <= 0) {
            qCWarning(logLlm, "ответ службы аутентификации не разобран");
            done(false);
            return;
        }

        m_accessToken = token;
        m_tokenExpiry = QDateTime::currentDateTimeUtc().addSecs(lifetime);
        qCInfo(logLlm, "токен доступа получен, годен %d с", lifetime);
        done(true);
    });

    return true;
}

void LlmClient::checkHealth()
{
    if (needsToken()) {
        ensureAccessToken([this](bool ok) {
            if (ok)
                sendHealthRequest();
            else
                emit healthChecked(false, false, tr("не удалось получить токен доступа"));
        });
        return;
    }

    sendHealthRequest();
}

void LlmClient::sendHealthRequest()
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

    const QString authorization = authorizationValue();
    if (!authorization.isEmpty())
        request.setRawHeader("Authorization", authorization.toUtf8());

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
