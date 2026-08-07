#include "secretstore.h"

#include <KWallet>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(logSecret, "openpet.secret")

namespace {

const QString kFolder = QStringLiteral("open-pet");

// Кошелёк открывается синхронно и только по требованию: при обычном запуске
// без настроенной LLM он не трогается вовсе, и пользователя не спрашивают
// пароль на ровном месте.
KWallet::Wallet *openWallet()
{
    return KWallet::Wallet::openWallet(KWallet::Wallet::LocalWallet(), 0,
                                       KWallet::Wallet::Synchronous);
}

} // namespace

bool SecretStore::isAvailable()
{
    return KWallet::Wallet::isEnabled();
}

bool SecretStore::store(const QString &account, const QString &secret)
{
    if (!isAvailable()) {
        // Молчать нельзя: пользователь должен узнать, что ключ не сохранён,
        // а не обнаружить это при следующем запуске.
        qCWarning(logSecret, "кошелёк недоступен, ключ не сохранён");
        return false;
    }

    std::unique_ptr<KWallet::Wallet> wallet(openWallet());
    if (!wallet) {
        qCWarning(logSecret, "кошелёк не открылся, ключ не сохранён");
        return false;
    }

    if (!wallet->hasFolder(kFolder) && !wallet->createFolder(kFolder))
        return false;

    if (!wallet->setFolder(kFolder))
        return false;

    // Значение секрета в журнал не попадает никогда — ни при успехе,
    // ни при ошибке.
    return wallet->writePassword(account, secret) == 0;
}

QString SecretStore::read(const QString &account)
{
    if (!isAvailable())
        return {};

    std::unique_ptr<KWallet::Wallet> wallet(openWallet());
    if (!wallet || !wallet->setFolder(kFolder))
        return {};

    QString secret;
    if (wallet->readPassword(account, secret) != 0)
        return {};

    return secret;
}

bool SecretStore::remove(const QString &account)
{
    if (!isAvailable())
        return false;

    std::unique_ptr<KWallet::Wallet> wallet(openWallet());
    if (!wallet || !wallet->setFolder(kFolder))
        return false;

    return wallet->removeEntry(account) == 0;
}
