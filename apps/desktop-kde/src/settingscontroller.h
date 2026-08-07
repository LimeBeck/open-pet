#pragma once

#include "settings.h"

#include <QObject>
#include <QString>
#include <QUrl>

class CoreBridge;
class LlmClient;

// Модель окна настроек (§4.1, §9).
//
// Отдельно от PetViewModel намеренно: у питомца на экране и у окна настроек
// разные жизненные циклы, и мешать их — верный способ держать окно живым
// ради одного свойства.
class SettingsController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString corner READ corner WRITE setCorner NOTIFY changed)
    Q_PROPERTY(int marginRight READ marginRight WRITE setMarginRight NOTIFY changed)
    Q_PROPERTY(int marginBottom READ marginBottom WRITE setMarginBottom NOTIFY changed)
    Q_PROPERTY(qreal scale READ scale WRITE setScale NOTIFY changed)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY changed)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY changed)
    Q_PROPERTY(int idleSeconds READ idleSeconds WRITE setIdleSeconds NOTIFY changed)

    Q_PROPERTY(bool sourceIdle READ sourceIdle WRITE setSourceIdle NOTIFY changed)
    Q_PROPERTY(bool sourcePower READ sourcePower WRITE setSourcePower NOTIFY changed)
    Q_PROPERTY(bool sourceSession READ sourceSession WRITE setSourceSession NOTIFY changed)
    Q_PROPERTY(bool sourceMedia READ sourceMedia WRITE setSourceMedia NOTIFY changed)
    Q_PROPERTY(bool sourceNotification READ sourceNotification WRITE setSourceNotification NOTIFY changed)
    Q_PROPERTY(bool sourceActiveApp READ sourceActiveApp WRITE setSourceActiveApp NOTIFY changed)

    Q_PROPERTY(int llmKind READ llmKind WRITE setLlmKind NOTIFY changed)
    Q_PROPERTY(QString llmBaseUrl READ llmBaseUrl WRITE setLlmBaseUrl NOTIFY changed)
    Q_PROPERTY(QString llmModel READ llmModel WRITE setLlmModel NOTIFY changed)
    Q_PROPERTY(int llmTimeoutMs READ llmTimeoutMs WRITE setLlmTimeoutMs NOTIFY changed)
    Q_PROPERTY(int proxyMode READ proxyMode WRITE setProxyMode NOTIFY changed)
    Q_PROPERTY(QString proxyHost READ proxyHost WRITE setProxyHost NOTIFY changed)
    Q_PROPERTY(int proxyPort READ proxyPort WRITE setProxyPort NOTIFY changed)
    Q_PROPERTY(QString proxyUser READ proxyUser WRITE setProxyUser NOTIFY changed)
    Q_PROPERTY(bool proxyBypassLocal READ proxyBypassLocal WRITE setProxyBypassLocal NOTIFY changed)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY changed)
    Q_PROPERTY(bool secretStorageAvailable READ secretStorageAvailable CONSTANT)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY changed)

    Q_PROPERTY(QString restartNotice READ restartNotice NOTIFY changed)
    Q_PROPERTY(QString healthStatus READ healthStatus NOTIFY changed)
    Q_PROPERTY(QString packStatus READ packStatus NOTIFY changed)
    Q_PROPERTY(QString activePackId READ activePackId NOTIFY changed)

public:
    explicit SettingsController(CoreBridge *core, LlmClient *llm, QObject *parent = nullptr);

    QString corner() const;
    void setCorner(const QString &corner);
    int marginRight() const { return m_settings.marginRight; }
    void setMarginRight(int value);
    int marginBottom() const { return m_settings.marginBottom; }
    void setMarginBottom(int value);
    qreal scale() const { return m_settings.scale; }
    void setScale(qreal value);
    bool reducedMotion() const { return m_settings.reducedMotion; }
    void setReducedMotion(bool value);
    bool paused() const { return m_settings.paused; }
    void setPaused(bool value);
    int idleSeconds() const { return m_settings.idleSeconds; }
    void setIdleSeconds(int value);

    bool sourceIdle() const { return m_settings.sourceIdle; }
    void setSourceIdle(bool value);
    bool sourcePower() const { return m_settings.sourcePower; }
    void setSourcePower(bool value);
    bool sourceSession() const { return m_settings.sourceSession; }
    void setSourceSession(bool value);
    bool sourceMedia() const { return m_settings.sourceMedia; }
    void setSourceMedia(bool value);
    bool sourceNotification() const { return m_settings.sourceNotification; }
    void setSourceNotification(bool value);
    bool sourceActiveApp() const { return m_settings.sourceActiveApp; }
    void setSourceActiveApp(bool value);

    int llmKind() const { return m_settings.llmKind; }
    void setLlmKind(int value);
    QString llmBaseUrl() const { return m_settings.llmBaseUrl; }
    void setLlmBaseUrl(const QString &value);
    QString llmModel() const { return m_settings.llmModel; }
    void setLlmModel(const QString &value);
    int llmTimeoutMs() const { return m_settings.llmTimeoutMs; }
    void setLlmTimeoutMs(int value);

    int proxyMode() const { return m_settings.proxyMode; }
    void setProxyMode(int value);
    QString proxyHost() const { return m_settings.proxyHost; }
    void setProxyHost(const QString &value);
    int proxyPort() const { return m_settings.proxyPort; }
    void setProxyPort(int value);
    QString proxyUser() const { return m_settings.proxyUser; }
    void setProxyUser(const QString &value);
    bool proxyBypassLocal() const { return m_settings.proxyBypassLocal; }
    void setProxyBypassLocal(bool value);

    // Пароль прокси уходит в KWallet тем же путём, что и ключ API.
    Q_INVOKABLE bool storeProxyPassword(const QString &password);

    // Автозапуск не хранится в наших настройках: источник истины — наличие
    // файла в каталоге автозапуска, который пользователь может убрать
    // штатными средствами системы мимо нас.
    bool autostart() const;
    void setAutostart(bool value);

    bool secretStorageAvailable() const;
    bool hasApiKey() const { return m_hasApiKey; }

    QString restartNotice() const { return m_restartNotice; }
    QString healthStatus() const { return m_healthStatus; }

    // Проверка связи с провайдером (§FR-7). Результат придёт асинхронно
    // в healthStatus: ждать сеть синхронно значит подвесить окно.
    Q_INVOKABLE void checkConnection();

    QString packStatus() const { return m_packStatus; }
    QString activePackId() const;

    // §US-07: пользователь выбирает файл, валидатор проверяет его до
    // установки, негодный отклоняется с понятным списком ошибок.
    Q_INVOKABLE void importPack(const QUrl &fileUrl);
    Q_INVOKABLE void resetPackToBuiltin();

    // §9: перед первым включением сетевого провайдера UI показывает точный
    // пример payload. Здесь показывается не выдуманный образец, а ровно то
    // тело, которое отправит ядро.
    Q_INVOKABLE QString payloadPreview() const;

    // Ключ уходит прямо в KWallet и не хранится в этом объекте дольше вызова.
    Q_INVOKABLE bool storeApiKey(const QString &key);
    Q_INVOKABLE bool forgetApiKey();

    Q_INVOKABLE void apply();
    // §9: удаляет настройки и историю, не трогая импортированные Pet Pack.
    Q_INVOKABLE void resetLocalData();

    const Settings &current() const { return m_settings; }

signals:
    void changed();
    void applied();
    void localDataReset();
    // Хост меняет питомца на экране: путь пустой — вернуться к встроенному.
    void petPackChanged(const QString &sheetPath);

private:
    void markDirty(bool needsRestart = false);

    CoreBridge *m_core = nullptr;
    LlmClient *m_llm = nullptr;
    Settings m_settings;
    bool m_hasApiKey = false;
    QString m_restartNotice;
    QString m_healthStatus;
    QString m_packStatus;
};
